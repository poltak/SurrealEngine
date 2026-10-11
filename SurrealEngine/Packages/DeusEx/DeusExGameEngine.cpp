
#include "Precomp.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Utils/Exception.h"
#include "Utils/StrTools.h"
#include "GC/GC.h"
#include "VM/ScriptCall.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Audio/AudioDevice.h"
#include "Render/RenderSubsystem.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/DeusEx/UDeusExLevelInfo.h"
#include "Packages/DeusEx/UDeusExSaveInfo.h"
#include "Packages/DeusEx/AIEventManager.h"
#include "Packages/Extension/UPlayerPawnExt.h"
#include "Packages/Extension/Flags/UFlagBase.h"
#include <filesystem>

// The Deus Ex save system. It follows DDeusExGameEngine in DeusEx.dll (build 1112fm):
//
// - Save/Current holds the saved state of each map that the player left in the current mission.
//   A map travel in the same mission saves the map that the player leaves to Save/Current, and it
//   loads the map that the player enters from Save/Current when a saved copy is there.
// - A travel to a different mission, a new game or a restart deletes the files in Save/Current.
// - A save game copies Save/Current to Save/QuickSave or Save/SaveNNNN, writes SaveInfo.dxs there
//   and saves the current map there.
// - A load game copies the slot folder back to Save/Current and loads the map that SaveInfo names,
//   with the options "load", "loadonly" and "loadgame". The game script reads "loadgame" in the
//   login and then keeps the saved player as it is. "loadonly" stops the travel of items.

namespace
{
	const int DXSaveCurrent = -2;
	const int DXSaveQuick = -1;

	std::string SlotFolderName(int directoryIndex)
	{
		if (directoryIndex == DXSaveCurrent)
			return "Current";
		if (directoryIndex == DXSaveQuick)
			return "QuickSave";
		char buf[32];
		snprintf(buf, sizeof(buf), "Save%04d", directoryIndex);
		return buf;
	}

	template<typename T>
	T* ObjectProperty(UObject* obj, const char* name)
	{
		PropertyDataOffset offset = obj->GetPropertyDataOffset(name);
		if (offset.DataOffset == ~(size_t)0)
			return nullptr;
		return UObject::TryCast<T>(obj->Value<UObject*>(offset));
	}

	void ClearObjectProperty(UObject* obj, const char* name)
	{
		PropertyDataOffset offset = obj->GetPropertyDataOffset(name);
		if (offset.DataOffset != ~(size_t)0)
			obj->Value<UObject*>(offset) = nullptr;
	}

	// The scripts do not keep one spelling for a map name. The file systems of Linux and some Macs do.
	fs::path FindFileIgnoreCase(const fs::path& directory, const std::string& filename)
	{
		std::error_code ec;
		if (!fs::is_directory(directory, ec))
			return {};
		for (const auto& entry : fs::directory_iterator(directory, ec))
		{
			if (entry.is_regular_file(ec) && StrTools::equals_ignore_case(entry.path().filename().string(), filename))
				return entry.path();
		}
		return {};
	}

	// Destroys the actors of a chain (FirstAug or FirstSkill), then the manager actor
	void DestroyManager(UObject* player, const char* managerName, const char* firstName)
	{
		UActor* manager = ObjectProperty<UActor>(player, managerName);
		if (!manager)
			return;

		UActor* cur = ObjectProperty<UActor>(manager, firstName);
		while (cur)
		{
			UActor* next = ObjectProperty<UActor>(cur, "next");
			cur->Destroy();
			cur = next;
		}

		manager->Destroy();
		ClearObjectProperty(player, managerName);
	}
}

fs::path Engine::DXSaveDirectory(int directoryIndex) const
{
	return packages->GetSaveFolderPath() / SlotFolderName(directoryIndex);
}

void Engine::DXDeleteSaveGameFiles(const fs::path& directory)
{
	std::error_code ec;
	if (!fs::is_directory(directory, ec))
		return;

	Array<fs::path> files;
	for (const auto& entry : fs::directory_iterator(directory, ec))
	{
		if (entry.is_regular_file(ec))
			files.push_back(entry.path());
	}
	for (const fs::path& file : files)
		fs::remove(file, ec);
}

void Engine::DXCopySaveGameFiles(const fs::path& source, const fs::path& dest)
{
	std::error_code ec;
	fs::create_directories(dest, ec);
	if (!fs::is_directory(source, ec))
		return;

	std::string saveExt = "." + packages->GetSaveExtension();
	for (const auto& entry : fs::directory_iterator(source, ec))
	{
		if (!entry.is_regular_file(ec))
			continue;
		if (!StrTools::equals_ignore_case(entry.path().extension().string(), saveExt))
			continue;
		fs::copy_file(entry.path(), dest / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
	}
}

int Engine::DXGetNextMissionNumber(const std::string& mapName) const
{
	// The first two characters of a mission map name are the mission number
	if (mapName.size() < 2 || !std::isdigit((unsigned char)mapName[0]) || !std::isdigit((unsigned char)mapName[1]))
		return -1;
	return (mapName[0] - '0') * 10 + (mapName[1] - '0');
}

void Engine::DXPruneTravelActors()
{
	// The augmentations, the skills, the flags and a carried decoration travel with the player.
	// They must not stay in the saved copy of the map that the player leaves.
	UPlayerPawn* player = viewport ? UObject::TryCast<UPlayerPawn>(viewport->Actor()) : nullptr;
	if (!player)
		return;

	DestroyManager(player, "AugmentationSystem", "FirstAug");
	DestroyManager(player, "SkillSystem", "FirstSkill");

	if (auto playerExt = UObject::TryCast<UPlayerPawnExt>(player))
	{
		if (playerExt->FlagBase())
		{
			playerExt->FlagBase()->DeleteAllFlags();
			playerExt->FlagBase() = nullptr;
		}
	}

	if (UActor* decoration = ObjectProperty<UActor>(player, "carriedDecoration"))
	{
		decoration->Destroy();
		ClearObjectProperty(player, "carriedDecoration");
	}
}

void Engine::DXSaveCurrentLevel(int directoryIndex)
{
	if (!Level || !LevelPackage || !DeusExLevelInfo)
	{
		LogMessage("Deus Ex save: no DeusExLevelInfo in this map. The map is not saved.");
		return;
	}

	fs::path directory = DXSaveDirectory(directoryIndex);
	std::error_code ec;
	fs::create_directories(directory, ec);

	fs::path filename = directory / (DeusExLevelInfo->MapName() + "." + packages->GetSaveExtension());
	LevelPackage->Save(Level, filename.string());

	// The package writer keeps the file that it replaced as ".old". The save folders must not collect those.
	fs::remove(fs::path(filename.string() + ".old"), ec);
}

void Engine::DXSaveGame(int directoryIndex, const std::string& description)
{
	UPlayerPawn* player = viewport ? UObject::TryCast<UPlayerPawn>(viewport->Actor()) : nullptr;
	if (!player || !Level)
		return;
	if (!DeusExLevelInfo)
	{
		LogMessage("Deus Ex save: no DeusExLevelInfo in this map. The game is not saved.");
		return;
	}

	UClass* saveInfoClass = packages->FindClass("DeusEx.DeusExSaveInfo");
	if (!saveInfoClass)
	{
		LogMessage("Deus Ex save: could not find the class DeusExSaveInfo");
		return;
	}

	Package* saveInfoPackage = GC::Alloc<Package>(packages.get(), "SaveInfo", std::string());
	saveInfoPackage->CopyFormatFrom(LevelPackage);
	UDXSaveInfo* saveInfo = UObject::Cast<UDXSaveInfo>(saveInfoPackage->NewObject("MyDeusExSaveInfo", saveInfoClass, ObjectFlags::NoFlags));

	saveInfo->MapName() = DeusExLevelInfo->MapName();
	saveInfo->Description() = description.empty() ? DeusExLevelInfo->MissionLocation() : description;
	saveInfo->MissionLocation() = DeusExLevelInfo->MissionLocation();
	saveInfo->DirectoryIndex() = directoryIndex;

	if (player->HasProperty("saveCount"))
	{
		int count = (int)player->GetInt("saveCount") + 1;
		player->SetInt("saveCount", count);
		saveInfo->SaveCount() = count;
	}
	if (player->HasProperty("saveTime"))
		saveInfo->SaveTime() = (int)player->GetFloat("saveTime");
	saveInfo->UpdateTimeStamp();
	saveInfo->bCheatsEnabled() = player->HasProperty("bCheatsEnabled") && player->GetBool("bCheatsEnabled");
	saveInfo->Snapshot() = nullptr; // To do: the 160x120 picture of the scene

	// Index 0 asks for a new slot: the highest number of a save folder, plus one
	std::error_code ec;
	if (directoryIndex == 0)
	{
		int highest = 0;
		fs::path saveFolder = packages->GetSaveFolderPath();
		if (fs::is_directory(saveFolder, ec))
		{
			for (const auto& entry : fs::directory_iterator(saveFolder, ec))
			{
				std::string name = entry.path().filename().string();
				if (entry.is_directory(ec) && name.size() > 4 && StrTools::startswith(name, "Save", true))
					highest = std::max(highest, std::atoi(name.c_str() + 4));
			}
		}
		saveInfo->DirectoryIndex() = highest + 1;
	}

	fs::path slotDirectory = DXSaveDirectory(saveInfo->DirectoryIndex());
	fs::create_directories(slotDirectory, ec);

	DXDeleteSaveGameFiles(slotDirectory);
	DXCopySaveGameFiles(DXSaveDirectory(DXSaveCurrent), slotDirectory);

	fs::path saveInfoFile = slotDirectory / ("SaveInfo." + packages->GetSaveExtension());
	saveInfoPackage->Save(saveInfo, saveInfoFile.string());
	fs::remove(fs::path(saveInfoFile.string() + ".old"), ec);

	DXSaveCurrentLevel(saveInfo->DirectoryIndex());
	LogMessage("Deus Ex save: saved the game to " + SlotFolderName(saveInfo->DirectoryIndex()));
}

void Engine::DXLoadSavedLevel(Package* package, const UnrealURL& url, const std::map<std::string, std::string>& travelInfo)
{
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	audiodev->StopSounds();
	UnloadMap();

	LevelPackage = package;

	GetLevelInfoObject();

	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0;
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = 0; // NM_Standalone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;
	LevelInfo->URL = url;

	GetLevelObject();

	Level->TravelInfo = travelInfo;

	LinkActorsToLevel();

	// A saved map has its GameInfo actor and has begun play. The begin play events must not run again.
	GameInfo = UObject::Cast<UGameInfo>(LevelInfo->Game());
	if (!GameInfo)
		Exception::Throw("Saved map has no GameInfo actor: " + LevelPackage->GetPackageName().ToString());

	// The event manager is not part of the saved map. The script made it in LevelInfo.PreBeginPlay, which does not run again.
	if (!Level->EventManager)
		Level->EventManager = std::make_unique<AIEventManager>(Level);

	// UGameEngine::LoadMap of the Deus Ex Engine.dll sends PostPostBeginPlay to each actor of each map that it loads,
	// also when the map has begun play before. The actors bind their conversations there, because ConListItems is transient.
	size_t actorCount = Level->Actors.size();
	for (size_t i = 0; i < actorCount; i++)
	{
		if (Level->Actors[i])
			CallEvent(Level->Actors[i], "PostPostBeginPlay");
	}

	CallEvent(GameInfo, "DetailChange", {});
}

// Does a client travel as DDeusExGameEngine::Browse does it. Returns false if the normal map travel must do the work.
bool Engine::DXBrowse()
{
	UnrealURL url(ClientTravelInfo.URL);
	std::string saveExt = packages->GetSaveExtension();
	std::error_code ec;

	if (url.HasOption("restart"))
	{
		// Start the current map again from the original map file. The saved map states of the mission are lost.
		if (!DeusExLevelInfo)
			return false;
		UnrealURL restartUrl(LevelInfo->URL);
		restartUrl.Map = DeusExLevelInfo->MapName();
		restartUrl.AddOrReplaceOption("loadonly");
		DXDeleteSaveGameFiles(DXSaveDirectory(DXSaveCurrent));
		LogMessage("Deus Ex travel: restart of " + restartUrl.Map);
		LoadMap(restartUrl, {});
		LoginPlayer();
		LevelInfo->URL.RemoveOption("loadonly");
		return true;
	}

	if (url.HasOption("loadgame"))
	{
		ClientTravelInfo.URL.Clear();

		int directoryIndex = std::atoi(url.GetOption("loadgame").c_str());
		fs::path slotDirectory = DXSaveDirectory(directoryIndex);
		fs::path saveInfoFile = slotDirectory / ("SaveInfo." + saveExt);
		if (!fs::exists(saveInfoFile, ec))
		{
			LogMessage("Deus Ex load: no save game in " + SlotFolderName(directoryIndex));
			return true;
		}

		std::string mapName;
		{
			Package* saveInfoPackage = GC::Alloc<Package>(packages.get(), "SaveInfo", saveInfoFile.string());
			UDXSaveInfo* saveInfo = UObject::TryCast<UDXSaveInfo>(saveInfoPackage->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo"));
			if (saveInfo)
				mapName = saveInfo->MapName();
			packages->UnloadPackage(saveInfoPackage);
		}
		if (mapName.empty())
		{
			LogMessage("Deus Ex load: could not read the save info in " + SlotFolderName(directoryIndex));
			return true;
		}

		fs::path currentDirectory = DXSaveDirectory(DXSaveCurrent);
		DXDeleteSaveGameFiles(currentDirectory);
		DXCopySaveGameFiles(slotDirectory, currentDirectory);

		fs::path mapFile = FindFileIgnoreCase(currentDirectory, mapName + "." + saveExt);
		if (mapFile.empty())
		{
			LogMessage("Deus Ex load: the save game has no file for the map " + mapName);
			return true;
		}

		UnrealURL loadUrl(LevelInfo->URL);
		loadUrl.Map = mapName;
		loadUrl.Portal.clear();
		loadUrl.AddOrReplaceOption("load");
		loadUrl.AddOrReplaceOption("loadonly");
		loadUrl.AddOrReplaceOption("loadgame");

		LogMessage("Deus Ex load: " + SlotFolderName(directoryIndex) + ", map " + mapName);
		Package* levelPackage = GC::Alloc<Package>(packages.get(), mapName, mapFile.string());
		DXLoadSavedLevel(levelPackage, loadUrl, {});
		LoginPlayer();

		// The load options are for this login only
		LevelInfo->URL.RemoveOption("load");
		LevelInfo->URL.RemoveOption("loadonly");
		LevelInfo->URL.RemoveOption("loadgame");
		return true;
	}

	if (url.Map.empty())
		return false;

	// The map name without its folder and its extension
	std::string mapName = url.Map;
	size_t slashPos = mapName.find_last_of("/\\");
	if (slashPos != std::string::npos)
		mapName = mapName.substr(slashPos + 1);
	size_t extPos = mapName.rfind('.');
	if (extPos != std::string::npos)
		mapName = mapName.substr(0, extPos);

	int currentMission = DeusExLevelInfo ? DeusExLevelInfo->MissionNumber() : -1;
	int nextMission = DXGetNextMissionNumber(mapName);
	LogMessage("Deus Ex travel to " + mapName + ": the current mission number is " + std::to_string(currentMission) + ", the next is " + std::to_string(nextMission));

	UPlayerPawn* player = viewport ? UObject::TryCast<UPlayerPawn>(viewport->Actor()) : nullptr;
	bool startingNewGame = player && player->HasProperty("bStartingNewGame") && player->GetBool("bStartingNewGame");

	if (nextMission == -1 || url.HasOption("loadonly") || nextMission != currentMission || startingNewGame)
	{
		// A map that is not part of a mission, a different mission, or a new game. The saved map states are not kept.
		DXDeleteSaveGameFiles(DXSaveDirectory(DXSaveCurrent));
		if (startingNewGame && nextMission != -1 && !url.HasOption("loadonly"))
			player->SetBool("bStartingNewGame", false);
		return false;
	}

	// Travel in the same mission. Keep the state of the map that the player leaves.
	auto travelInfo = CreateTravelInfo(ClientTravelInfo.TransferItems);
	DXPruneTravelActors();
	DXSaveCurrentLevel(DXSaveCurrent);

	fs::path savedFile = FindFileIgnoreCase(DXSaveDirectory(DXSaveCurrent), mapName + "." + saveExt);
	if (savedFile.empty())
	{
		LogMessage("Client travel to " + url.ToString());
		LoadMap(url, travelInfo);
		LoginPlayer();
		return true;
	}

	LogMessage("Client travel to " + url.ToString() + " (saved map state)");
	Package* levelPackage = GC::Alloc<Package>(packages.get(), mapName, savedFile.string());
	DXLoadSavedLevel(levelPackage, url, travelInfo);
	LoginPlayer();
	return true;
}
