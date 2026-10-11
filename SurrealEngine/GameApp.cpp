
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
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

	bool harnessNoLauncher = false; // must outlive the try block: "cmd" is gone when the catch handler runs

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [Path to game folder]\n";
			return 0;
		}

		// Local test harness (fork only): --logfile=<path> streams the log to a file,
		// --nolauncher starts the first game folder given on the command line.
		std::string harnessLogFile = commandline->GetArg({}, "--logfile");
		if (!harnessLogFile.empty())
		{
			static FILE* logfile = fopen(harnessLogFile.c_str(), "w");
			Logger::Get()->SetCallback([](const LogMessageLine& line) {
				if (!logfile) return;
				fprintf(logfile, "[%.2f] %s: %s\n", line.Time, line.Source.empty() ? "<SE>" : line.Source.c_str(), line.Text.c_str());
				fflush(logfile);
			});
		}

		int selectedGameIndex = -1;
		if (commandline->HasArg({}, "--nolauncher"))
		{
			harnessNoLauncher = true;
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
		commandline = nullptr;
		if (harnessNoLauncher)
		{
			LogMessage(std::string("FATAL: ") + e.what());
			std::cerr << "FATAL: " << e.what() << std::endl;
			return 1;
		}
		ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
