
#include "Precomp.h"
#include "UDeusExPlayer.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/DeusEx/UGameDirectory.h"
#include "Packages/ConSys/History/UConHistory.h"
#include "Utils/Logger.h"

void UDeusExPlayer::ConBindEvents()
{
	DeusExConBindEvents();
}

// Makes an object that is part of the game state of this player. The object is in the package of the
// player with the player as its outer object, so that a save of the level includes it.
UObject* UDeusExPlayer::CreatePlayerStateObject(const char* className)
{
	auto cls = engine->packages->FindClass(className);
	UObject* obj = package->NewObject(NameString(), cls, ObjectFlags::NoFlags);
	obj->Outer() = this;
	return obj;
}

UObject* UDeusExPlayer::CreateDataVaultImageNoteObject()
{
	return CreatePlayerStateObject("DeusEx.DataVaultImageNote");
}

UObject* UDeusExPlayer::CreateDumpLocationObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DumpLocation");
	return engine->packages->GetTransientPackage()->NewObject("DumpLocation", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateGameDirectoryObject()
{
	if (!m_GameDirectory)
	{
		auto cls = engine->packages->FindClass("DeusEx.GameDirectory");
		m_GameDirectory = Cast<UDXGameDirectory>(engine->packages->GetTransientPackage()->NewObject("GameDirectory", cls, ObjectFlags::Transient));
	}

	return m_GameDirectory;
}

UObject* UDeusExPlayer::CreateHistoryEvent()
{
	return CreatePlayerStateObject("ConSys.ConHistoryEvent");
}

UObject* UDeusExPlayer::CreateHistoryObject()
{
	return CreatePlayerStateObject("ConSys.ConHistory");
}

UObject* UDeusExPlayer::CreateLogObject()
{
	return CreatePlayerStateObject("DeusEx.DeusExLog");
}

void UDeusExPlayer::DeleteSaveGameFiles(std::optional<std::string> saveDirectory)
{
	// No directory means Save/Current, the saved map states of the current mission
	if (saveDirectory && !saveDirectory->empty())
		engine->DXDeleteSaveGameFiles(fs::path(*saveDirectory));
	else
		engine->DXDeleteSaveGameFiles(engine->DXSaveDirectory(-2));
}

std::string UDeusExPlayer::GetDeusExVersion()
{
	return "1.112fm. Surreal Engine Edition!";
}

void UDeusExPlayer::SaveGame(int saveIndex, std::optional<std::string> saveDesc)
{
	engine->SaveGameInfo.SaveGameSlot = saveIndex;
	engine->SaveGameInfo.SaveGameDescription = saveDesc ? *saveDesc : std::string();
}

NameString UDeusExPlayer::SetBoolFlagFromString(const std::string& flagNameString, bool bValue)
{
	// Not called directly from script
	LogUnimplemented("DeusExPlayer.SetBoolFlagFromString");
	return {};
}

void UDeusExPlayer::UnloadTexture(UObject* Texture)
{
	// Nothing going on here because SE never unloads textures atm. This is just here so it doesn't throw LogUnimplemented.
}
