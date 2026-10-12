
#include "Precomp.h"
#include "DevLoaderWindow.h"
#include "GameFolder.h"
#include "Harness.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include <surrealwidgets/core/image.h>
#include <surrealwidgets/core/canvas.h>
#include <surrealwidgets/core/timer.h>
#include <surrealwidgets/window/window.h>
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/widgets/listview/listview.h>
#include <surrealwidgets/widgets/dropdown/dropdown.h>
#include <surrealwidgets/widgets/checkboxlabel/checkboxlabel.h>
#include <surrealwidgets/widgets/lineedit/lineedit.h>
#include <surrealwidgets/widgets/pushbutton/pushbutton.h>
#include <surrealwidgets/widgets/layout/vboxlayout.h>
#include <surrealwidgets/widgets/layout/hboxlayout.h>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <set>

namespace
{
	struct NamedClass
	{
		const char* ClassName;
		const char* Label;
	};

	// The weapons of the game, in the order of the checkboxes (3 columns, top to bottom)
	const NamedClass Weapons[] =
	{
		{ "WeaponPistol", "Pistol" },
		{ "WeaponStealthPistol", "Stealth pistol" },
		{ "WeaponMiniCrossbow", "Mini-crossbow" },
		{ "WeaponAssaultGun", "Assault rifle" },
		{ "WeaponAssaultShotgun", "Assault shotgun" },
		{ "WeaponSawedOffShotgun", "Sawed-off shotgun" },
		{ "WeaponRifle", "Sniper rifle" },
		{ "WeaponGEPGun", "GEP gun" },

		{ "WeaponFlamethrower", "Flamethrower" },
		{ "WeaponPlasmaRifle", "Plasma rifle" },
		{ "WeaponLAW", "LAW rocket" },
		{ "WeaponHideAGun", "PS20" },
		{ "WeaponPepperGun", "Pepper gun" },
		{ "WeaponProd", "Riot prod" },
		{ "WeaponBaton", "Baton" },
		{ "WeaponCombatKnife", "Combat knife" },

		{ "WeaponCrowbar", "Crowbar" },
		{ "WeaponSword", "Sword" },
		{ "WeaponNanoSword", "Dragon's Tooth sword" },
		{ "WeaponShuriken", "Throwing knives" },
		{ "WeaponLAM", "LAM" },
		{ "WeaponGasGrenade", "Gas grenade" },
		{ "WeaponEMPGrenade", "EMP grenade" },
		{ "WeaponNanoVirusGrenade", "Scrambler grenade" },
	};
	const int WeaponCount = sizeof(Weapons) / sizeof(Weapons[0]);
	const int WeaponRows = 8;

	// Things that the player can have more than one of
	const NamedClass Things[] =
	{
		{ "MedKit", "Medkits" },
		{ "BioelectricCell", "Bioelectric cells" },
		{ "Lockpick", "Lockpicks" },
		{ "Multitool", "Multitools" },
	};
	const int ThingCount = sizeof(Things) / sizeof(Things[0]);

	struct WeaponSet
	{
		const char* Label;
		Array<const char*> ClassNames;
		bool All = false;
	};

	// The entries of the weapon set list after its first entry
	const WeaponSet WeaponSets[] =
	{
		{ "No weapons", {} },
		{ "New game", { "WeaponPistol", "WeaponProd" } },
		{ "Quiet", { "WeaponStealthPistol", "WeaponMiniCrossbow", "WeaponProd", "WeaponBaton", "WeaponPepperGun", "WeaponGasGrenade" } },
		{ "Heavy", { "WeaponPistol", "WeaponAssaultGun", "WeaponAssaultShotgun", "WeaponRifle", "WeaponGEPGun", "WeaponNanoSword", "WeaponLAM" } },
		{ "All weapons", {}, true },
	};
	const int WeaponSetCount = sizeof(WeaponSets) / sizeof(WeaponSets[0]);

	const char* SkillLevels[] = { "As a new game", "All untrained", "All trained", "All advanced", "All master" };
	const char* Difficulties[] = { "Easy", "Medium", "Hard", "Realistic" };

	std::string ToLower(std::string text)
	{
		for (char& c : text)
			c = (char)std::tolower((unsigned char)c);
		return text;
	}

	bool IsNameChar(uint8_t c)
	{
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
	}

	// A widget with a layout in it, so that a row or a column can have a fixed size
	Widget* MakeBox(Widget* parent, Layout* layout)
	{
		Widget* box = new Widget(parent);
		box->SetLayout(layout);
		return box;
	}

	TextLabel* MakeHeading(Widget* parent, const std::string& text)
	{
		TextLabel* label = new TextLabel(parent);
		label->SetText(text);
		label->SetStyleColor("color", Colorf::fromRgba8(160, 175, 255));
		return label;
	}

	// A row gives each widget its full height. This keeps a widget at its own height, in the middle of the row.
	Widget* MakeCentered(Widget* parent, Widget* widget)
	{
		auto layout = new VBoxLayout();
		layout->SetGapHeight(0.0);
		layout->AddStretch();
		layout->AddWidget(widget);
		layout->AddStretch();
		return MakeBox(parent, layout);
	}

	Widget* MakeSpace(Widget* parent, double width, double height)
	{
		Widget* space = new Widget(parent);
		space->SetFixedSize(width, height);
		return space;
	}
}

DevLoaderWindow::Result DevLoaderWindow::ExecModal(const GameLaunchInfo& info, DevLoadout& loadout, const Options& options)
{
	Size screenSize = GetScreenSize();
	double windowWidth = 1100.0;
	double windowHeight = 700.0;

	auto window = std::make_unique<DevLoaderWindow>(info, loadout, options);
	window->SetFrameGeometry((screenSize.width - windowWidth) * 0.5, (screenSize.height - windowHeight) * 0.5, windowWidth, windowHeight);
	window->Show();

	DisplayWindow::RunLoop();

	if (window->WindowResult == Result::Start)
		loadout = window->Selections;
	return window->WindowResult;
}

DevLoaderWindow::DevLoaderWindow(const GameLaunchInfo& info, const DevLoadout& loadout, const Options& options) : Widget(nullptr, WidgetType::Window), WindowOptions(options)
{
	SetWindowTitle("Deus Ex developer loader");
	SetWindowIcon({
		Image::LoadResource("surreal-engine-icon-16.png"),
		Image::LoadResource("surreal-engine-icon-24.png"),
		Image::LoadResource("surreal-engine-icon-32.png"),
		Image::LoadResource("surreal-engine-icon-48.png"),
		Image::LoadResource("surreal-engine-icon-64.png"),
		Image::LoadResource("surreal-engine-icon-128.png"),
		Image::LoadResource("surreal-engine-icon-256.png")
		});
	SetNoncontentSizes(20, 16, 20, 16);

	std::string mapsFolder = (fs::path(info.gameRootFolder) / "Maps").string();
	Maps = FindMaps(mapsFolder);
	StartPoints = ScanStartPoints(mapsFolder, Maps);

	// The texts at the top
	TitleLabel = new TextLabel(this);
	TitleLabel->SetText("Developer loader");
	TitleLabel->SetStyleClass("devloader-title");
	IntroLabel = new TextLabel(this);
	IntroLabel->SetText("Select a map and the things of the player. Start puts the player in that map, with no menu and no intro. Main menu starts the game as usual.");

	// Left column: the map
	MapLabel = MakeHeading(this, "Map");
	MapList = new ListView(this);
	MapList->ShowHeader(true);
	MapList->SetColumn(0, "Mission", 245.0);
	MapList->SetColumn(1, "Map file", 235.0);
	for (const std::string& map : Maps)
		MapList->AddItem({ GetMissionTitle(map), map });
	MapList->OnChanged = [this](int) { UpdateStartPoints({}); };
	MapList->OnActivated = [this]() { Start(); };

	StartPointLabel = new TextLabel(this);
	StartPointLabel->SetText("Start point");
	StartPointList = new Dropdown(this);
	StartPointList->SetMaxDisplayItems(12);
	StartPointList->SetDropdownDirection(false);

	auto startPointLayout = new HBoxLayout();
	startPointLayout->AddWidget(StartPointLabel);
	startPointLayout->AddWidget(MakeSpace(this, 8, 1));
	StartPointList->SetStretching(true);
	startPointLayout->AddWidget(StartPointList);

	auto leftLayout = new VBoxLayout();
	leftLayout->AddWidget(MapLabel);
	leftLayout->AddWidget(MapList);
	leftLayout->AddLayout(startPointLayout);
	Widget* leftColumn = MakeBox(this, leftLayout);
	leftColumn->SetFixedWidth(510.0);

	// Right column: the weapons
	WeaponsLabel = MakeHeading(this, "Weapons");
	WeaponSetList = new Dropdown(this);
	WeaponSetList->AddItem("Select a set");
	for (const WeaponSet& set : WeaponSets)
		WeaponSetList->AddItem(set.Label);
	WeaponSetList->SetSelectedItem(0);
	WeaponSetList->OnChanged = [this](int index) { ApplyWeaponSet(index); };

	auto weaponsHeadLayout = new HBoxLayout();
	weaponsHeadLayout->AddWidget(WeaponsLabel);
	weaponsHeadLayout->AddStretch();
	weaponsHeadLayout->AddWidget(WeaponSetList);

	auto weaponColumnsLayout = new HBoxLayout();
	for (int first = 0; first < WeaponCount; first += WeaponRows)
	{
		auto columnLayout = new VBoxLayout();
		columnLayout->SetGapHeight(6.0);
		for (int i = first; i < std::min(first + WeaponRows, WeaponCount); i++)
		{
			CheckboxLabel* box = new CheckboxLabel(this);
			box->SetText(Weapons[i].Label);
			WeaponBoxes.push_back(box);
			columnLayout->AddWidget(box);
		}
		Widget* column = MakeBox(this, columnLayout);
		column->SetFixedWidth(170.0);
		weaponColumnsLayout->AddWidget(column);
	}
	weaponColumnsLayout->AddStretch();

	FullAmmoBox = new CheckboxLabel(this);
	FullAmmoBox->SetText("Full ammunition for each weapon, with its special types");

	// The things with a count
	ThingsLabel = MakeHeading(this, "Other things");
	auto thingsLayout = new HBoxLayout();
	for (int i = 0; i < ThingCount; i++)
	{
		TextLabel* label = new TextLabel(this);
		label->SetText(Things[i].Label);
		LineEdit* edit = new LineEdit(this);
		edit->SetNumericMode(true);
		edit->SetMaxLength(2);
		edit->SetIntrinsicSize(3);
		ThingLabels.push_back(label);
		ThingCounts.push_back(edit);
		thingsLayout->AddWidget(label);
		thingsLayout->AddWidget(edit);
		thingsLayout->AddWidget(MakeSpace(this, 10, 1));
	}
	thingsLayout->AddStretch();

	// The values of the player
	PlayerLabel = MakeHeading(this, "Player");
	SkillsLabel = new TextLabel(this);
	SkillsLabel->SetText("Skills");
	SkillsList = new Dropdown(this);
	for (const char* text : SkillLevels)
		SkillsList->AddItem(text);
	DifficultyLabel = new TextLabel(this);
	DifficultyLabel->SetText("Difficulty");
	DifficultyList = new Dropdown(this);
	for (const char* text : Difficulties)
		DifficultyList->AddItem(text);
	CreditsLabel = new TextLabel(this);
	CreditsLabel->SetText("Credits");
	CreditsEdit = new LineEdit(this);
	CreditsEdit->SetNumericMode(true);
	CreditsEdit->SetMaxLength(6);
	CreditsEdit->SetIntrinsicSize(7);

	auto playerLayout = new HBoxLayout();
	playerLayout->AddWidget(SkillsLabel);
	playerLayout->AddWidget(SkillsList);
	playerLayout->AddWidget(MakeSpace(this, 10, 1));
	playerLayout->AddWidget(DifficultyLabel);
	playerLayout->AddWidget(DifficultyList);
	playerLayout->AddWidget(MakeSpace(this, 10, 1));
	playerLayout->AddWidget(CreditsLabel);
	playerLayout->AddWidget(MakeCentered(this, CreditsEdit));
	playerLayout->AddStretch();

	AugmentationsBox = new CheckboxLabel(this);
	AugmentationsBox->SetText("All augmentations, at their top level");
	GodModeBox = new CheckboxLabel(this);
	GodModeBox->SetText("God mode: the player takes no damage");
	CheatsBox = new CheckboxLabel(this);
	CheatsBox->SetText("Cheat commands in the console of the game");

	NoteLabel = new TextLabel(this);
	NoteLabel->SetText("A thing that has no room in the inventory lies at the feet of the player.");
	NoteLabel->SetStyleColor("color", Colorf::fromRgba8(170, 170, 180));

	auto rightLayout = new VBoxLayout();
	rightLayout->SetGapHeight(8.0);
	rightLayout->AddLayout(weaponsHeadLayout);
	rightLayout->AddLayout(weaponColumnsLayout);
	rightLayout->AddWidget(FullAmmoBox);
	rightLayout->AddWidget(MakeSpace(this, 1, 6));
	rightLayout->AddWidget(ThingsLabel);
	rightLayout->AddLayout(thingsLayout);
	rightLayout->AddWidget(MakeSpace(this, 1, 6));
	rightLayout->AddWidget(PlayerLabel);
	rightLayout->AddLayout(playerLayout);
	rightLayout->AddWidget(AugmentationsBox);
	rightLayout->AddWidget(GodModeBox);
	rightLayout->AddWidget(CheatsBox);
	rightLayout->AddStretch();
	rightLayout->AddWidget(NoteLabel);
	Widget* rightColumn = MakeBox(this, rightLayout);
	rightColumn->SetStretching(true);

	auto columnsLayout = new HBoxLayout();
	columnsLayout->AddWidget(leftColumn);
	columnsLayout->AddWidget(MakeSpace(this, 24, 1));
	columnsLayout->AddWidget(rightColumn);
	Widget* columns = MakeBox(this, columnsLayout);
	columns->SetStretching(true);

	// The buttons
	StartButton = new PushButton(this);
	StartButton->SetText("Start");
	StartButton->OnClick = [this]() { Start(); };
	MainMenuButton = new PushButton(this);
	MainMenuButton->SetText("Main menu");
	MainMenuButton->OnClick = [this]() { Close(Result::MainMenu); };
	QuitButton = new PushButton(this);
	QuitButton->SetText("Quit");
	QuitButton->OnClick = [this]() { Close(Result::Quit); };

	auto buttonLayout = new HBoxLayout();
	buttonLayout->AddWidget(StartButton);
	buttonLayout->AddStretch();
	buttonLayout->AddWidget(MainMenuButton);
	buttonLayout->AddWidget(MakeSpace(this, 8, 1));
	buttonLayout->AddWidget(QuitButton);

	auto mainLayout = new VBoxLayout();
	mainLayout->SetGapHeight(8.0);
	mainLayout->AddWidget(TitleLabel);
	mainLayout->AddWidget(IntroLabel);
	mainLayout->AddWidget(MakeSpace(this, 1, 4));
	mainLayout->AddWidget(columns);
	mainLayout->AddWidget(MakeSpace(this, 1, 4));
	mainLayout->AddLayout(buttonLayout);
	SetLayout(mainLayout);

	SetLoadout(loadout);
	MapList->SetFocus();

	LogMessage("Developer loader: the window has " + std::to_string(Maps.size()) + " maps");

	// The list has no size before the window opens, and a scroll position needs the size
	ScrollTimer = new Timer(this);
	ScrollTimer->FuncExpired = [this]()
	{
		// Some maps before the selected map stay in view
		int index = MapList->GetSelectedItem();
		if (index >= 0)
		{
			MapList->ScrollToItem(std::max(index - 3, 0));
			MapList->ScrollToItem(index);
		}
	};
	ScrollTimer->Start(50, false);

	if (!WindowOptions.ScriptFile.empty())
	{
		for (const std::string& line : File::read_all_lines(WindowOptions.ScriptFile))
		{
			if (!line.empty() && line[0] != '#')
				ScriptLines.push_back(line);
		}

		// One command for each step. The window has its first picture a short time after it opens.
		ScriptTimer = new Timer(this);
		ScriptTimer->FuncExpired = [this]()
		{
			if (NextScriptLine < ScriptLines.size())
				RunScriptLine(ScriptLines[NextScriptLine++]);
			else
				ScriptTimer->Stop();
		};
		ScriptTimer->Start(250, true);
	}
}

DevLoaderWindow::~DevLoaderWindow()
{
	delete ScrollTimer;
	delete ScriptTimer;
}

// A test script of the window. Each line is one command:
//   click X Y            a click of the left mouse button at this place of the window, in points from its top left corner
//   key CODE [CHARS]     a key with this macOS key code that types CHARS (51 = Backspace)
//   shot NAME            write a picture of the window to NAME.bmp in the picture folder of the test harness (--shots)
//   log                  write the selections of the window to the log
//   wait                 do nothing in this step
// A click on the Start button ends the script, because the window closes.
void DevLoaderWindow::RunScriptLine(const std::string& line)
{
	std::istringstream parts(line);
	std::string command;
	parts >> command;

	if (command == "click")
	{
		double x = 0.0, y = 0.0;
		parts >> x >> y;
		// The mouse comes to the place first, as the real mouse does. The list of a dropdown stays open only while
		// the mouse is on it.
		InjectNativeMouseEvent(2, x, y);
		InjectNativeMouseEvent(0, x, y);
		InjectNativeMouseEvent(1, x, y);
	}
	else if (command == "key")
	{
		int code = 0;
		std::string chars;
		parts >> code >> chars;
		InjectNativeKeyEvent(0, code, 0, chars);
		InjectNativeKeyEvent(1, code, 0, chars);
	}
	else if (command == "shot")
	{
		std::string name;
		parts >> name;
		std::string folder = Harness::Get().GetShotFolder();
		if (!name.empty() && !folder.empty())
			WriteShot((fs::path(folder) / (name + ".bmp")).string());
	}
	else if (command == "log")
	{
		DevLoadout loadout = GetLoadout();
		std::string items;
		for (const DevLoadoutItem& item : loadout.Items)
			items += " " + item.ClassName + "*" + std::to_string(item.Count);
		LogMessage("Developer loader: selections url=" + loadout.GetUrl() + " items=" + (items.empty() ? " none" : items) +
			" fullAmmo=" + std::to_string((int)loadout.FullAmmo) + " skills=" + std::to_string(loadout.SkillLevel) +
			" augmentations=" + std::to_string((int)loadout.AllAugmentations) + " credits=" + std::to_string(loadout.Credits) +
			" difficulty=" + std::to_string(loadout.Difficulty) + " god=" + std::to_string((int)loadout.GodMode) +
			" cheats=" + std::to_string((int)loadout.Cheats) + " startPoints=" + std::to_string(CurrentTags.size()));
	}
}

void DevLoaderWindow::WriteShot(const std::string& filename)
{
	int width = 0, height = 0;
	std::vector<uint32_t> pixels;
	Canvas* canvas = GetCanvas();
	if (!canvas || !canvas->getPixels(width, height, pixels))
	{
		LogMessage("Developer loader: the window has no picture to write");
		return;
	}

	// A BMP file with 32 bits for each pixel and the top row first
	uint32_t dataSize = (uint32_t)width * height * 4;
	uint8_t header[54] = { 'B', 'M' };
	auto put32 = [&](int offset, uint32_t value) { memcpy(header + offset, &value, 4); };
	auto put16 = [&](int offset, uint16_t value) { memcpy(header + offset, &value, 2); };
	put32(2, 54 + dataSize); put32(10, 54); put32(14, 40);
	put32(18, (uint32_t)width); put32(22, (uint32_t)-height);
	put16(26, 1); put16(28, 32); put32(34, dataSize);

	Array<uint8_t> file(54 + dataSize);
	memcpy(file.data(), header, 54);
	memcpy(file.data() + 54, pixels.data(), dataSize);
	File::write_all_bytes(filename, file.data(), file.size());
	LogMessage("Developer loader: wrote a picture of the window, " + std::to_string(width) + "x" + std::to_string(height) + " pixels");
}

Array<std::string> DevLoaderWindow::FindMaps(const std::string& mapsFolder)
{
	Array<std::string> maps;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(mapsFolder, ec))
	{
		fs::path path = entry.path();
		if (ToLower(path.extension().string()) != ".dx")
			continue;

		// The entry map is the empty map that the engine keeps loaded. A player cannot stand in it.
		std::string name = path.stem().string();
		if (ToLower(name) == "entry")
			continue;
		maps.push_back(name);
	}

	// The missions in their order, then the maps with no mission number
	std::sort(maps.begin(), maps.end(), [](const std::string& a, const std::string& b)
	{
		bool numberA = !a.empty() && std::isdigit((unsigned char)a[0]);
		bool numberB = !b.empty() && std::isdigit((unsigned char)b[0]);
		if (numberA != numberB)
			return numberA;
		return ToLower(a) < ToLower(b);
	});
	return maps;
}

std::string DevLoaderWindow::GetMissionTitle(const std::string& map)
{
	static const std::map<std::string, std::string> titles =
	{
		{ "00", "Intro and training" },
		{ "01", "Liberty Island" },
		{ "02", "Battery Park, Hell's Kitchen" },
		{ "03", "Airfield" },
		{ "04", "NSF headquarters" },
		{ "05", "Escape from UNATCO" },
		{ "06", "Hong Kong" },
		{ "08", "Return to New York" },
		{ "09", "Shipyard and graveyard" },
		{ "10", "Paris" },
		{ "11", "Cathedral and Everett" },
		{ "12", "Vandenberg" },
		{ "14", "Ocean lab and silo" },
		{ "15", "Area 51" },
		{ "99", "Endings" },
	};

	std::string number = map.substr(0, 2);
	auto it = titles.find(number);
	if (it != titles.end())
		return number + " " + it->second;
	if (ToLower(map).rfind("dxmp", 0) == 0)
		return "Multiplayer map";
	if (map.size() > 2 && std::isdigit((unsigned char)map[0]) && std::isdigit((unsigned char)map[1]))
		return number;
	return "Other map";
}

// A map exit keeps its destination as text in the map file: the name of the map, "#", and the tag of the
// teleporter at which the player comes into that map. The files are read as bytes, with no load of a map.
std::map<std::string, Array<std::string>> DevLoaderWindow::ScanStartPoints(const std::string& mapsFolder, const Array<std::string>& maps)
{
	std::map<std::string, Array<std::string>> result;
	std::set<std::string> names;
	std::set<size_t> nameLengths;
	for (const std::string& map : maps)
	{
		names.insert(ToLower(map));
		nameLengths.insert(map.size());
	}

	std::map<std::string, std::set<std::string>> found;
	std::string candidate;
	for (const std::string& map : maps)
	{
		Array<uint8_t> data;
		try
		{
			data = File::read_all_bytes((fs::path(mapsFolder) / (map + ".dx")).string());
		}
		catch (...)
		{
			continue;
		}

		const uint8_t* begin = data.data();
		const uint8_t* end = begin + data.size();
		const uint8_t* pos = begin;
		while (pos < end && (pos = static_cast<const uint8_t*>(std::memchr(pos, '#', end - pos))) != nullptr)
		{
			const uint8_t* hash = pos++;

			size_t tagLength = 0;
			while (hash + 1 + tagLength < end && tagLength < 40 && IsNameChar(hash[1 + tagLength]))
				tagLength++;
			if (tagLength == 0)
				continue;

			// The byte before the name is the length of the text, and it can look like a part of the name.
			// So try each length that a map name can have.
			for (size_t length : nameLengths)
			{
				if ((size_t)(hash - begin) < length)
					continue;
				candidate.assign(reinterpret_cast<const char*>(hash - length), length);
				candidate = ToLower(candidate);
				if (names.find(candidate) != names.end())
				{
					found[candidate].insert(std::string(reinterpret_cast<const char*>(hash + 1), tagLength));
					break;
				}
			}
		}
	}

	for (auto& it : found)
		result[it.first] = Array<std::string>(it.second.begin(), it.second.end());
	return result;
}

void DevLoaderWindow::UpdateStartPoints(const std::string& selectedTag)
{
	StartPointList->ClearItems();
	StartPointList->AddItem("Default start of the map");
	CurrentTags.clear();

	int index = MapList->GetSelectedItem();
	if (index >= 0 && index < (int)Maps.size())
	{
		auto it = StartPoints.find(ToLower(Maps[index]));
		if (it != StartPoints.end())
			CurrentTags = it->second;
	}

	int selected = 0;
	for (size_t i = 0; i < CurrentTags.size(); i++)
	{
		StartPointList->AddItem(CurrentTags[i]);
		if (!selectedTag.empty() && ToLower(CurrentTags[i]) == ToLower(selectedTag))
			selected = (int)i + 1;
	}
	StartPointList->SetSelectedItem(selected);
}

void DevLoaderWindow::ApplyWeaponSet(int index)
{
	// The first entry is the name of the list
	if (index <= 0 || index > WeaponSetCount)
		return;

	const WeaponSet& set = WeaponSets[index - 1];
	for (int i = 0; i < WeaponCount; i++)
	{
		bool checked = set.All;
		for (const char* className : set.ClassNames)
		{
			if (std::strcmp(className, Weapons[i].ClassName) == 0)
				checked = true;
		}
		WeaponBoxes[i]->SetChecked(checked);
	}
	WeaponSetList->SetSelectedItem(0);
}

void DevLoaderWindow::SetLoadout(const DevLoadout& loadout)
{
	int mapIndex = -1;
	for (size_t i = 0; i < Maps.size(); i++)
	{
		if (ToLower(Maps[i]) == ToLower(loadout.Map))
			mapIndex = (int)i;
	}
	if (mapIndex < 0 && !Maps.empty())
		mapIndex = 0;
	if (mapIndex >= 0)
	{
		MapList->SetSelectedItem(mapIndex, false);
		MapList->ScrollToItem(mapIndex);
	}
	UpdateStartPoints(loadout.StartTag);

	std::vector<int> thingCounts(ThingCount, 0);
	OtherItems.clear();
	for (const DevLoadoutItem& item : loadout.Items)
	{
		bool known = false;
		for (int i = 0; i < WeaponCount; i++)
		{
			if (ToLower(item.ClassName) == ToLower(Weapons[i].ClassName))
			{
				WeaponBoxes[i]->SetChecked(true);
				known = true;
			}
		}
		for (int i = 0; i < ThingCount; i++)
		{
			if (ToLower(item.ClassName) == ToLower(Things[i].ClassName))
			{
				thingCounts[i] += item.Count;
				known = true;
			}
		}
		if (!known)
			OtherItems.push_back(item);
	}
	for (int i = 0; i < ThingCount; i++)
		ThingCounts[i]->SetTextInt(std::min(thingCounts[i], 99));

	FullAmmoBox->SetChecked(loadout.FullAmmo);
	SkillsList->SetSelectedItem(std::clamp(loadout.SkillLevel, -1, 3) + 1);
	DifficultyList->SetSelectedItem(std::clamp(loadout.Difficulty, 0, 3));
	// A new player of the game has 500 credits
	CreditsEdit->SetTextInt(loadout.Credits >= 0 ? std::min(loadout.Credits, 999999) : 500);
	AugmentationsBox->SetChecked(loadout.AllAugmentations);
	GodModeBox->SetChecked(loadout.GodMode);
	CheatsBox->SetChecked(loadout.Cheats);
}

DevLoadout DevLoaderWindow::GetLoadout() const
{
	DevLoadout loadout;
	loadout.Items.clear();

	int mapIndex = MapList->GetSelectedItem();
	if (mapIndex >= 0 && mapIndex < (int)Maps.size())
		loadout.Map = Maps[mapIndex];

	int startIndex = StartPointList->GetSelectedItem();
	if (startIndex >= 1 && startIndex <= (int)CurrentTags.size())
		loadout.StartTag = CurrentTags[startIndex - 1];

	for (int i = 0; i < WeaponCount; i++)
	{
		if (WeaponBoxes[i]->GetChecked())
			loadout.Items.push_back({ Weapons[i].ClassName, 1 });
	}
	for (int i = 0; i < ThingCount; i++)
	{
		int count = ThingCounts[i]->GetText().empty() ? 0 : ThingCounts[i]->GetTextInt();
		if (count > 0)
			loadout.Items.push_back({ Things[i].ClassName, count });
	}
	for (const DevLoadoutItem& item : OtherItems)
		loadout.Items.push_back(item);

	loadout.FullAmmo = FullAmmoBox->GetChecked();
	loadout.SkillLevel = SkillsList->GetSelectedItem() - 1;
	loadout.Difficulty = std::max(DifficultyList->GetSelectedItem(), 0);
	loadout.Credits = CreditsEdit->GetText().empty() ? -1 : CreditsEdit->GetTextInt();
	loadout.AllAugmentations = AugmentationsBox->GetChecked();
	loadout.GodMode = GodModeBox->GetChecked();
	loadout.Cheats = CheatsBox->GetChecked();
	return loadout;
}

void DevLoaderWindow::Start()
{
	if (Closed || MapList->GetSelectedItem() < 0)
		return;

	Selections = GetLoadout();
	LogMessage("Developer loader: the window starts " + Selections.GetUrl() + " with " + std::to_string(Selections.Items.size()) + " kinds of things");

	if (WindowOptions.SaveSelections)
	{
		try
		{
			Selections.SaveFile(DevLoadout::GetSettingsFilename());
		}
		catch (const std::exception& e)
		{
			LogMessage(std::string("Developer loader: could not save the selections: ") + e.what());
		}
	}

	Close(Result::Start);
}

void DevLoaderWindow::Close(Result result)
{
	if (Closed)
		return;
	Closed = true;
	WindowResult = result;
	if (result == Result::MainMenu)
		LogMessage("Developer loader: the window goes on to the main menu");
	DisplayWindow::ExitLoop();
}

void DevLoaderWindow::OnClose()
{
	Close(Result::Quit);
}
