
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "Harness.h"
#include "DevLoader.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "UI/DevLoader/DevLoaderWindow.h"
#include "Utils/File.h"
#include <stdexcept>
#include <iostream>

// Developer loader of the deus-ex-mac fork, see DevLoader.h. It selects the map and the things of the player for
// this start. Returns false when the person closed the loader window, so that no game starts.
static bool SetupDevLoader(GameLaunchInfo& info)
{
	if (!info.IsDeusEx())
		return true;

	std::string file = commandline->GetArg({}, "--loadout");
	bool showWindow = commandline->HasArg({}, "--loader");

	// A person who holds the Option key while the app starts gets the loader window. A test run has no person.
	if (!showWindow && file.empty() && !Harness::Get().NoLauncher)
		showWindow = DevLoader::IsLoaderKeyDown();

	if (!showWindow && file.empty())
		return true;

	DevLoadout loadout;
	if (!file.empty())
	{
		loadout = DevLoadout::LoadFile(file);
	}
	else
	{
		// The selections of the last time. The first time has no file.
		try
		{
			loadout = DevLoadout::LoadFile(DevLoadout::GetSettingsFilename());
		}
		catch (...)
		{
		}
	}

	if (showWindow)
	{
		DevLoaderWindow::Options options;
		options.SaveSelections = file.empty();
		options.ScriptFile = commandline->GetArg({}, "--loader-script");
		DevLoaderWindow::Result result = DevLoaderWindow::ExecModal(info, loadout, options);
		if (result == DevLoaderWindow::Result::Quit)
			return false;
		if (result == DevLoaderWindow::Result::MainMenu)
			return true;
	}

	DevLoader::Get().Active = true;
	DevLoader::Get().Loadout = loadout;

	// A map on the command line comes before the map of a loadout file. The map of the window comes before the two.
	if (showWindow || !commandline->HasArg("-u", "--url"))
		info.url = loadout.GetUrl();
	return true;
}

int GameApp::main(Array<std::string> args)
{
	InitWidgetResources("dark");

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;

		// Test harness of the deus-ex-mac fork, see Harness.h
		Harness::Get().Init();

		// A harness run must not open the crash report window: it would stay on the screen with no person to close it.
		if (!Harness::Get().NoLauncher && ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--loader] [--loadout=<file>] [Path to game folder]\n";
			return 0;
		}

		int selectedGameIndex = -1;
		if (commandline->HasArg({}, "--direct"))
		{
			// Start the game in the folder from the command line with no launcher window.
			// The Deus Ex app for macOS uses this: its start script finds the game folder of the player.
			// The engine must not start a different game that it finds on this computer.
			GameFolderSelection::UpdateList(true);
			if (GameFolderSelection::Games.empty())
				Exception::Throw("No known game was found in the game folder. The folder must have the System and Maps folders of the game in it.");
			selectedGameIndex = 0;
		}
		else if (Harness::Get().NoLauncher)
		{
			GameFolderSelection::UpdateList();
			selectedGameIndex = GameFolderSelection::Games.empty() ? -1 : 0;
			if (selectedGameIndex < 0)
				LogMessage("Harness: no known game found in the given folder");
		}
		else
		{
			selectedGameIndex = LauncherWindow::ExecModal();
		}
		if (selectedGameIndex >= 0)
		{
			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
			if (SetupDevLoader(info))
			{
				Engine engine(info);
				engine.Run();
			}
		}
	}
	catch (const std::exception& e)
	{
		commandline = nullptr; // "cmd" is gone when this handler runs
		if (Harness::Get().NoLauncher)
		{
			Harness::Get().LogFatal(e.what());
			return 1;
		}
		ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
