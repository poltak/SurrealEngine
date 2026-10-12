#pragma once

#include <surrealwidgets/core/widget.h>
#include <map>
#include "DevLoader.h"

class TextLabel;
class ListView;
class Dropdown;
class CheckboxLabel;
class LineEdit;
class PushButton;
class Timer;
struct GameLaunchInfo;

// The window of the developer loader (see DevLoader.h). A person selects a map, a start point in the map, the
// weapons and the values of the player. The Start button starts the game with them.
class DevLoaderWindow : public Widget
{
public:
	struct Options
	{
		bool SaveSelections = true;   // Keep the selections for the next time
		std::string ScriptFile;       // For a test: a file with commands that use the window with no person, see RunScriptLine
	};

	// What the person did with the window
	enum class Result
	{
		Start,     // The Start button: start the game with the selections
		MainMenu,  // The Main menu button: start the game as it starts with no loader
		Quit       // The Quit button, or the window was closed: start no game
	};

	// Shows the window and waits. "loadout" gives the first selections and gets the selections of the person.
	static Result ExecModal(const GameLaunchInfo& info, DevLoadout& loadout, const Options& options);

	DevLoaderWindow(const GameLaunchInfo& info, const DevLoadout& loadout, const Options& options);
	~DevLoaderWindow();

	// The travel addresses (map#tag) that the map exits of the game use, for each map in lower case.
	// The tag of an address is a start point of that map.
	static std::map<std::string, Array<std::string>> ScanStartPoints(const std::string& mapsFolder, const Array<std::string>& maps);

	// The names of the map files of the game folder, without the extension
	static Array<std::string> FindMaps(const std::string& mapsFolder);

	// The mission of a map, from the number at the start of its file name
	static std::string GetMissionTitle(const std::string& map);

private:
	void OnClose() override;

	void SetLoadout(const DevLoadout& loadout);
	DevLoadout GetLoadout() const;
	void UpdateStartPoints(const std::string& selectedTag);
	void ApplyWeaponSet(int index);
	void Start();
	void Close(Result result);
	void RunScriptLine(const std::string& line);
	void WriteShot(const std::string& filename);

	Options WindowOptions;
	Array<std::string> Maps;
	std::map<std::string, Array<std::string>> StartPoints;
	Array<std::string> CurrentTags;      // The tags of the start point list, after its first entry
	Array<DevLoadoutItem> OtherItems;    // Things of the loadout that the window has no control for. They stay in the loadout.

	TextLabel* TitleLabel = nullptr;
	TextLabel* IntroLabel = nullptr;
	TextLabel* MapLabel = nullptr;
	ListView* MapList = nullptr;
	TextLabel* StartPointLabel = nullptr;
	Dropdown* StartPointList = nullptr;
	TextLabel* WeaponsLabel = nullptr;
	Dropdown* WeaponSetList = nullptr;
	Array<CheckboxLabel*> WeaponBoxes;
	CheckboxLabel* FullAmmoBox = nullptr;
	TextLabel* ThingsLabel = nullptr;
	Array<TextLabel*> ThingLabels;
	Array<LineEdit*> ThingCounts;
	TextLabel* PlayerLabel = nullptr;
	TextLabel* SkillsLabel = nullptr;
	Dropdown* SkillsList = nullptr;
	TextLabel* DifficultyLabel = nullptr;
	Dropdown* DifficultyList = nullptr;
	TextLabel* CreditsLabel = nullptr;
	LineEdit* CreditsEdit = nullptr;
	CheckboxLabel* AugmentationsBox = nullptr;
	CheckboxLabel* GodModeBox = nullptr;
	CheckboxLabel* CheatsBox = nullptr;
	TextLabel* NoteLabel = nullptr;
	PushButton* StartButton = nullptr;
	PushButton* MainMenuButton = nullptr;
	PushButton* QuitButton = nullptr;
	Timer* ScrollTimer = nullptr;
	Timer* ScriptTimer = nullptr;
	Array<std::string> ScriptLines;
	size_t NextScriptLine = 0;

	Result WindowResult = Result::Quit;
	bool Closed = false;
	DevLoadout Selections;
};
