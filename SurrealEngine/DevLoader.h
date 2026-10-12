#pragma once

#include <string>

class Engine;
class UActor;
class UObject;

// Developer loader of the deus-ex-mac fork. It starts Deus Ex in a selected map, with selected weapons and
// player values, and without the menus and the intro. A person selects them in the loader window
// (UI/DevLoader/DevLoaderWindow). A file can give them also, which is how a test uses the loader.
//
// Command-line options:
//   --loader             open the loader window before the game starts. On macOS the window opens also when
//                        the Option key is down while the program starts.
//   --loadout=<file>     start with the selections of this JSON file and with no window. The file has the
//                        format that the loader window saves. With --loader, the window opens with the
//                        selections of the file. With --url, the map of --url comes before the map of the file.
//   --loader-script=<file>  with --loader: a test script that uses the window with no person
//                        (clicks, keys, pictures of the window, see DevLoaderWindow::RunScriptLine)
//
// The game starts with --url as a test does: a new player comes into the map at the selected start point.
// The loader removes the map states of the current game (Save/Current), as a new game does. The saved games
// of the player stay as they are.

// One thing for the inventory of the player
struct DevLoadoutItem
{
	std::string ClassName; // Class of the Deus Ex package, for example WeaponPistol
	int Count = 1;
};

// The selections of one start
struct DevLoadout
{
	std::string Map = "01_NYC_UNATCOIsland"; // File name of the map without the extension
	std::string StartTag;                    // Tag of the start point (the part after # of a travel address). Empty: the default start of the map
	// Weapons and other things, in the order in which the player gets them. The default is what a new game gives.
	Array<DevLoadoutItem> Items = { { "WeaponPistol", 1 }, { "WeaponProd", 1 }, { "MedKit", 1 } };
	bool FullAmmo = true;                    // Each weapon gets all of its ammunition types, and each type is full
	int SkillLevel = -1;                     // 0 to 3: each skill gets this level. -1: the skills stay as the game gives them
	bool AllAugmentations = false;           // The player gets each augmentation that has a free slot, at its top level
	int Credits = -1;                        // -1: the credits stay as the game gives them
	int Difficulty = 1;                      // 0 Easy, 1 Medium, 2 Hard, 3 Realistic
	bool GodMode = false;                    // The player takes no damage
	bool Cheats = true;                      // The cheat commands of the console stay on in the game

	// The address that the engine starts with
	std::string GetUrl() const;

	std::string ToJson() const;
	static DevLoadout FromJson(const std::string& json);

	// The file that keeps the last selections of the loader window
	static std::string GetSettingsFilename();
	static DevLoadout LoadFile(const std::string& filename);
	void SaveFile(const std::string& filename) const;

	// The damage factor of the game for each difficulty (the values of the New Game menu)
	static float GetCombatDifficulty(int difficulty);
};

class DevLoader
{
public:
	static DevLoader& Get();

	// True when this start comes from the loader. The engine then calls the two functions below.
	bool Active = false;
	DevLoadout Loadout;

	// macOS: true when the Option key is down at this time
	static bool IsLoaderKeyDown();

	// Before the engine loads the first map
	void BeforeFirstMap(Engine* engine);

	// Each frame. Gives the player the selected things when the player is in the map.
	void Tick(Engine* engine);

private:
	void Apply(Engine* engine, UActor* player);
	bool GiveItem(Engine* engine, UActor* player, const std::string& className);
	void GiveAllAmmoTypes(Engine* engine, UActor* player);

	bool applied = false;
	bool started = false;
	float startTime = 0.0f;
};
