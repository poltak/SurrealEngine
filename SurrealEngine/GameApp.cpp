
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "Harness.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "Utils/File.h"
#include <stdexcept>
#include <iostream>

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
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [Path to game folder]\n";
			return 0;
		}

		int selectedGameIndex = -1;
		if (Harness::Get().NoLauncher)
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
			Engine engine(info);
			engine.Run();
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
