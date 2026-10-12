
#include "Precomp.h"
#include "DevLoader.h"
#include "Engine.h"
#include "Utils/JsonValue.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "Utils/Exception.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "VM/ExpressionValue.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"

std::string DevLoadout::GetUrl() const
{
	return StartTag.empty() ? Map : Map + "#" + StartTag;
}

std::string DevLoadout::ToJson() const
{
	Array<JsonValue> items;
	for (const DevLoadoutItem& item : Items)
	{
		JsonValue value = JsonValue::object();
		value["Class"] = JsonValue::string(item.ClassName);
		value["Count"] = JsonValue::number(item.Count);
		items.push_back(std::move(value));
	}

	JsonValue root = JsonValue::object();
	root["Map"] = JsonValue::string(Map);
	root["StartTag"] = JsonValue::string(StartTag);
	root["Items"] = JsonValue::array(std::move(items));
	root["FullAmmo"] = JsonValue::boolean(FullAmmo);
	root["SkillLevel"] = JsonValue::number(SkillLevel);
	root["AllAugmentations"] = JsonValue::boolean(AllAugmentations);
	root["Credits"] = JsonValue::number(Credits);
	root["Difficulty"] = JsonValue::number(Difficulty);
	root["GodMode"] = JsonValue::boolean(GodMode);
	root["Cheats"] = JsonValue::boolean(Cheats);
	return root.to_json(true);
}

// A value that the file does not have keeps its default
DevLoadout DevLoadout::FromJson(const std::string& json)
{
	const JsonValue root = JsonValue::parse(json);
	if (!root.is_object())
		Exception::Throw("The loadout file must have a JSON object in it");

	DevLoadout loadout;
	loadout.Items.clear();
	auto readString = [&](const char* name, std::string& value) { if (root[name].type() == JsonType::string) value = root[name].to_string(); };
	auto readBool = [&](const char* name, bool& value) { if (root[name].is_boolean()) value = root[name].to_boolean(); };
	auto readInt = [&](const char* name, int& value) { if (root[name].is_number()) value = root[name].to_int(); };

	readString("Map", loadout.Map);
	readString("StartTag", loadout.StartTag);
	readBool("FullAmmo", loadout.FullAmmo);
	readInt("SkillLevel", loadout.SkillLevel);
	readBool("AllAugmentations", loadout.AllAugmentations);
	readInt("Credits", loadout.Credits);
	readInt("Difficulty", loadout.Difficulty);
	readBool("GodMode", loadout.GodMode);
	readBool("Cheats", loadout.Cheats);

	if (root["Items"].is_array())
	{
		for (const JsonValue& value : root["Items"].items())
		{
			DevLoadoutItem item;
			if (value.type() == JsonType::string)
			{
				item.ClassName = value.to_string();
			}
			else if (value.is_object())
			{
				if (value["Class"].type() == JsonType::string)
					item.ClassName = value["Class"].to_string();
				if (value["Count"].is_number())
					item.Count = value["Count"].to_int();
			}
			if (!item.ClassName.empty() && item.Count > 0)
				loadout.Items.push_back(std::move(item));
		}
	}

	loadout.SkillLevel = clamp(loadout.SkillLevel, -1, 3);
	loadout.Difficulty = clamp(loadout.Difficulty, 0, 3);
	return loadout;
}

std::string DevLoadout::GetSettingsFilename()
{
	return (Directory::localAppData() / "SurrealEngine/DeusExLoader.json").string();
}

DevLoadout DevLoadout::LoadFile(const std::string& filename)
{
	return FromJson(File::read_all_text(filename));
}

void DevLoadout::SaveFile(const std::string& filename) const
{
	Directory::create(fs::path(filename).parent_path().string());
	File::write_all_text(filename, ToJson());
}

float DevLoadout::GetCombatDifficulty(int difficulty)
{
	static const float values[] = { 1.0f, 1.5f, 2.0f, 4.0f };
	return values[clamp(difficulty, 0, 3)];
}

/////////////////////////////////////////////////////////////////////////////

DevLoader& DevLoader::Get()
{
	static DevLoader loader;
	return loader;
}

#ifdef __APPLE__

// CGEventSource.h of CoreGraphics. The header itself cannot be here: it brings the Point and Rect types of macOS,
// which have the names of types of the engine.
extern "C" uint64_t CGEventSourceFlagsState(int32_t stateID);

bool DevLoader::IsLoaderKeyDown()
{
	const int32_t combinedSessionState = 0; // kCGEventSourceStateCombinedSessionState
	const uint64_t alternateMask = 0x00080000; // kCGEventFlagMaskAlternate
	return (CGEventSourceFlagsState(combinedSessionState) & alternateMask) != 0;
}

#else

bool DevLoader::IsLoaderKeyDown()
{
	return false;
}

#endif

void DevLoader::BeforeFirstMap(Engine* engine)
{
	if (!Active || !engine->LaunchInfo.IsDeusEx())
		return;

	// As a new game: the maps of this start have no saved state. Without this, a map exit could give the state
	// that the map had in the last game of the player.
	engine->DXDeleteSaveGameFiles(engine->DXSaveDirectory(-2));
	LogMessage("Developer loader: start in " + engine->LaunchInfo.url);
}

void DevLoader::Tick(Engine* engine)
{
	if (!Active || applied || !engine->LaunchInfo.IsDeusEx())
		return;

	UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
	if (!player || !player->IsA("DeusExPlayer") || !engine->LevelInfo)
		return;

	float now = engine->LevelInfo->TimeSeconds();
	if (!started)
	{
		started = true;
		startTime = now;
	}

	// The mission script of a map runs its start code 1 second after the map starts (MissionScript.Timer calls
	// FirstFrame). That code can change the inventory: the start of mission 5 takes all things of the player away.
	// The player gets the selected things after it.
	if (now - startTime < 1.1f)
		return;

	applied = true;
	Apply(engine, player);
}

static void RunConsoleCommand(Engine* engine, UActor* player, const std::string& command)
{
	ExpressionValue found = ExpressionValue::BoolValue(false);
	engine->ConsoleCommand(player, command, found.ToType<BitfieldBool&>());
	if (!found.ToBool())
		LogMessage("Developer loader: the game has no console command " + command);
}

void DevLoader::Apply(Engine* engine, UActor* player)
{
	// The cheat commands below do nothing when this is off
	player->SetBool("bCheatsEnabled", true);

	player->SetFloat("CombatDifficulty", DevLoadout::GetCombatDifficulty(Loadout.Difficulty));

	if (Loadout.Credits >= 0)
		player->SetInt("Credits", (uint32_t)Loadout.Credits);

	if (Loadout.SkillLevel >= 0)
	{
		// As SkillManager.AddAllSkills, but to the selected level
		UObject* skillSystem = player->GetUObject("SkillSystem");
		int count = 0;
		for (UObject* skill = skillSystem ? skillSystem->GetUObject("FirstSkill") : nullptr; skill && count < 100; skill = skill->GetUObject("next"))
		{
			skill->SetInt("CurrentLevel", (uint32_t)Loadout.SkillLevel);
			count++;
		}
	}

	if (Loadout.AllAugmentations)
		RunConsoleCommand(engine, player, "AllAugs");

	int given = 0, missing = 0;
	for (const DevLoadoutItem& item : Loadout.Items)
	{
		for (int i = 0; i < item.Count; i++)
		{
			if (GiveItem(engine, player, item.ClassName))
				given++;
			else
				missing++;
		}
	}

	if (Loadout.FullAmmo)
	{
		GiveAllAmmoTypes(engine, player);
		RunConsoleCommand(engine, player, "AllAmmo");
	}

	if (Loadout.GodMode && player->GetName("ReducedDamageType") != "All")
		RunConsoleCommand(engine, player, "God");

	player->SetBool("bCheatsEnabled", Loadout.Cheats);

	LogMessage("Developer loader: the player has " + std::to_string(given) + " of " + std::to_string(given + missing) + " selected things");
}

// Give the player one thing. The steps are those of a right click on the thing (DeusExPlayer.ParseRightClick), so
// the thing goes through the pickup rules of the game: room in the inventory, a place on the belt, ammunition that
// adds to the ammunition of the same type. A thing that the player cannot take stays at the feet of the player.
bool DevLoader::GiveItem(Engine* engine, UActor* player, const std::string& className)
{
	UClass* cls = engine->packages->FindClass(className.find('.') == std::string::npos ? "DeusEx." + className : className);
	if (!cls)
	{
		LogMessage("Developer loader: the game has no class " + className);
		return false;
	}

	// As DeusExPlayer.GiveInitialInventory: the thing starts at the place of the player
	UActor* item = player->Spawn(cls, {}, {}, {}, {});
	if (!item)
		item = player->Spawn(cls, {}, {}, {}, {}, true);
	if (!item || !item->IsA("Inventory"))
	{
		LogMessage("Developer loader: could not make a " + className);
		if (item)
			item->Destroy();
		return false;
	}

	UFunction* handleItemPickup = FindEventFunction(player, "HandleItemPickup");
	UFunction* findInventorySlot = FindEventFunction(player, "FindInventorySlot");
	if (!handleItemPickup || !findInventorySlot)
		return false;

	// HandleItemPickup calls DoFrob, which reads the FrobTarget of the player
	player->SetObject("FrobTarget", item);
	bool taken = Frame::Call(handleItemPickup, player, Array<ExpressionValue>{ ExpressionValue::ObjectValue(item), ExpressionValue::BoolValue(true) }).ToBool();
	if (taken && !item->bDeleteMe() && item->Owner() == player)
		Frame::Call(findInventorySlot, player, Array<ExpressionValue>{ ExpressionValue::ObjectValue(item), ExpressionValue::BoolValue(false) });
	player->SetObject("FrobTarget", nullptr);

	if (!taken)
		LogMessage("Developer loader: the player could not take a " + className + ". It is at the feet of the player.");
	return taken;
}

// Each weapon of the player names up to three ammunition types (DeusExWeapon.AmmoNames). A pickup gives only the
// first type. Give the other types also, so that the player can select them.
void DevLoader::GiveAllAmmoTypes(Engine* engine, UActor* player)
{
	Array<UClass*> ammoClasses;
	int count = 0;
	for (UInventory* item = player->Inventory(); item && count < 200; item = item->Inventory(), count++)
	{
		if (!item->IsA("DeusExWeapon") || !item->HasProperty("AmmoNames"))
			continue;

		UObject** names = static_cast<UObject**>(item->GetProperty("AmmoNames"));
		for (int i = 0; i < 3; i++)
		{
			UClass* ammoClass = names[i] ? names[i]->AsClass() : nullptr;
			if (ammoClass && std::find(ammoClasses.begin(), ammoClasses.end(), ammoClass) == ammoClasses.end())
				ammoClasses.push_back(ammoClass);
		}
	}

	for (UClass* ammoClass : ammoClasses)
	{
		// AmmoNone is the type of weapons with no ammunition. The player has an ammunition type when a thing of that class is in the inventory.
		if (ammoClass->Name == "AmmoNone")
			continue;

		bool hasType = false;
		count = 0;
		for (UInventory* item = player->Inventory(); item && count < 200; item = item->Inventory(), count++)
		{
			if (item->Class == ammoClass)
			{
				hasType = true;
				break;
			}
		}

		if (!hasType)
			GiveItem(engine, player, "DeusEx." + ammoClass->Name.ToString());
	}
}
