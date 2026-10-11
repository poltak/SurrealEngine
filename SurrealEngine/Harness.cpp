
#include "Precomp.h"
#include "Harness.h"
#include "Engine.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Utils/File.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

static FILE* harnessLogFile = nullptr;

Harness& Harness::Get()
{
	static Harness harness;
	return harness;
}

void Harness::Init()
{
	if (initialized || !commandline)
		return;
	initialized = true;

	NoLauncher = commandline->HasArg({}, "--nolauncher");
	NoGrab = commandline->HasArg({}, "--nograb");
	exitAfter = commandline->GetArgDouble({}, "--exit-after", 0.0);
	shotDir = commandline->GetArg({}, "--shots");
	shotInterval = commandline->GetArgDouble({}, "--shot-interval", 0.0);
	nextShot = commandline->GetArgDouble({}, "--shot-first", 2.0);

	std::string logFilename = commandline->GetArg({}, "--logfile");
	if (!logFilename.empty())
	{
		harnessLogFile = fopen(logFilename.c_str(), "w");
		Logger::Get()->SetCallback([](const LogMessageLine& line) {
			if (!harnessLogFile) return;
			fprintf(harnessLogFile, "[%.2f] %s: %s\n", line.Time, line.Source.empty() ? "<SE>" : line.Source.c_str(), line.Text.c_str());
			fflush(harnessLogFile);
		});
	}

	winSize = commandline->GetArg({}, "--winsize");

	std::string script = commandline->GetArg({}, "--script");
	if (!script.empty())
		LoadScript(script);
}

void Harness::LogFatal(const std::string& text)
{
	if (harnessLogFile)
	{
		fprintf(harnessLogFile, "FATAL: %s\n", text.c_str());
		fflush(harnessLogFile);
	}
	fprintf(stderr, "FATAL: %s\n", text.c_str());
}

void Harness::LoadScript(const std::string& filename)
{
	std::istringstream lines(File::read_all_text(filename));
	std::string line;
	while (std::getline(lines, line))
	{
		size_t comment = line.find('#');
		if (comment != std::string::npos)
			line = line.substr(0, comment);
		std::istringstream parts(line);
		Event e;
		if (!(parts >> e.Time >> e.Command))
			continue;
		std::getline(parts, e.Args);
		size_t first = e.Args.find_first_not_of(" \t");
		size_t last = e.Args.find_last_not_of(" \t\r");
		e.Args = (first == std::string::npos) ? std::string() : e.Args.substr(first, last - first + 1);
		events.push_back(e);
	}
	std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.Time < b.Time; });
}

static EInputKey ParseKey(const std::string& text)
{
	static const std::map<std::string, int> names =
	{
		{ "lmb", IK_LeftMouse }, { "rmb", IK_RightMouse }, { "mmb", IK_MiddleMouse },
		{ "backspace", IK_Backspace }, { "tab", IK_Tab }, { "enter", IK_Enter }, { "shift", IK_Shift },
		{ "ctrl", IK_Ctrl }, { "alt", IK_Alt }, { "esc", IK_Escape }, { "space", IK_Space },
		{ "pageup", IK_PageUp }, { "pagedown", IK_PageDown }, { "end", IK_End }, { "home", IK_Home },
		{ "left", IK_Left }, { "up", IK_Up }, { "right", IK_Right }, { "down", IK_Down },
		{ "insert", IK_Insert }, { "delete", IK_Delete }, { "tilde", IK_Tilde },
		{ "wheelup", IK_MouseWheelUp }, { "wheeldown", IK_MouseWheelDown }
	};

	std::string name;
	for (char c : text)
		name.push_back((char)std::tolower((unsigned char)c));

	auto it = names.find(name);
	if (it != names.end())
		return (EInputKey)it->second;
	if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z')
		return (EInputKey)(IK_A + (name[0] - 'a'));
	if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
		return (EInputKey)(IK_0 + (name[0] - '0'));
	if (name.size() >= 2 && name[0] == 'f' && std::isdigit((unsigned char)name[1]))
		return (EInputKey)(IK_F1 + std::atoi(name.c_str() + 1) - 1);
	return (EInputKey)std::strtol(name.c_str(), nullptr, 0);
}

static Point ParsePoint(Engine* engine, const std::string& args)
{
	double fx = 0.5, fy = 0.5;
	std::istringstream parts(args);
	parts >> fx >> fy;
	double scale = engine->window->GetDpiScale();
	return Point(fx * engine->window->GetPixelWidth() / scale, fy * engine->window->GetPixelHeight() / scale);
}

void Harness::RunEvent(Engine* engine, const Event& e)
{
	LogMessage("Harness: " + e.Command + " " + e.Args);

	if (e.Command == "keydown")
	{
		engine->OnWindowKeyDown(ParseKey(e.Args));
	}
	else if (e.Command == "keyup")
	{
		engine->OnWindowKeyUp(ParseKey(e.Args));
	}
	else if (e.Command == "key")
	{
		engine->OnWindowKeyDown(ParseKey(e.Args));
		events.insert(events.begin() + nextEvent, Event{ e.Time + 0.08, "keyup", e.Args });
	}
	else if (e.Command == "char")
	{
		engine->OnWindowKeyChar(e.Args);
	}
	else if (e.Command == "move")
	{
		engine->OnWindowMouseMove(ParsePoint(engine, e.Args));
	}
	else if (e.Command == "mousedown" || e.Command == "mouseup" || e.Command == "click" || e.Command == "rclick")
	{
		Point pos = ParsePoint(engine, e.Args);
		EInputKey button = (e.Command == "rclick") ? IK_RightMouse : IK_LeftMouse;
		engine->OnWindowMouseMove(pos);
		if (e.Command == "mouseup")
		{
			engine->OnWindowMouseUp(pos, button);
		}
		else
		{
			engine->OnWindowMouseDown(pos, button);
			if (e.Command != "mousedown")
				events.insert(events.begin() + nextEvent, Event{ e.Time + 0.08, (e.Command == "rclick") ? "rmouseup" : "mouseup", e.Args });
		}
	}
	else if (e.Command == "rmouseup")
	{
		Point pos = ParsePoint(engine, e.Args);
		engine->OnWindowMouseUp(pos, IK_RightMouse);
	}
	else if (e.Command == "raw")
	{
		int dx = 0, dy = 0;
		std::istringstream parts(e.Args);
		parts >> dx >> dy;
		engine->OnWindowRawMouseMove(dx, dy);
	}
	else if (e.Command == "exec")
	{
		BitfieldBool found;
		std::string result = engine->ConsoleCommand(engine->viewport ? (UObject*)engine->viewport->Actor() : nullptr, e.Args, found);
		if (!result.empty())
			LogMessage("Harness: exec result: " + result);
	}
	else if (e.Command == "shot")
	{
		pendingShots.push_back(e.Args.empty() ? "shot" : e.Args);
	}
	else if (e.Command == "log")
	{
	}
	else if (e.Command == "quit")
	{
		engine->quit = true;
	}
	else
	{
		LogMessage("Harness: unknown script command: " + e.Command);
	}
}

void Harness::Tick(Engine* engine)
{
	if (!initialized)
		return;

	// A tiling window manager can resize the window after it opens. Put the requested size back.
	if (!winSize.empty() && engine->TotalTime >= nextWinSizeCheck)
	{
		nextWinSizeCheck = engine->TotalTime + 0.5;
		int w = 0, h = 0;
		if (sscanf(winSize.c_str(), "%dx%d", &w, &h) == 2 && (engine->window->GetPixelWidth() != w || engine->window->GetPixelHeight() != h))
			engine->window->SetResolution(winSize);
	}

	while (nextEvent < events.size() && events[nextEvent].Time <= engine->TotalTime)
	{
		Event e = events[nextEvent++];
		RunEvent(engine, e);
	}

	if (exitAfter > 0.0 && engine->TotalTime >= exitAfter)
		engine->quit = true;
}

void Harness::WriteShot(Engine* engine, RenderDevice* device, const std::string& name)
{
	int w = engine->window->GetPixelWidth();
	int h = engine->window->GetPixelHeight();
	if (w <= 0 || h <= 0)
		return;

	std::vector<TextureColor> pixels((size_t)w * h);
	device->ReadPixels(pixels.data());

	std::string filename = shotDir + "/" + name + ".bmp";
	FILE* f = fopen(filename.c_str(), "wb");
	if (!f)
		return;

	uint32_t dataSize = (uint32_t)w * h * 4;
	uint8_t hdr[54] = { 'B', 'M' };
	auto put32 = [&](int o, uint32_t v) { memcpy(hdr + o, &v, 4); };
	auto put16 = [&](int o, uint16_t v) { memcpy(hdr + o, &v, 2); };
	put32(2, 54 + dataSize); put32(10, 54); put32(14, 40);
	put32(18, (uint32_t)w); put32(22, (uint32_t)-h);
	put16(26, 1); put16(28, 32); put32(34, dataSize);
	fwrite(hdr, 1, 54, f);
	fwrite(pixels.data(), 1, dataSize, f);
	fclose(f);
	LogMessage("Harness: wrote " + name + ".bmp");
}

void Harness::AfterFrame(Engine* engine, RenderDevice* device)
{
	// ReadPixels submits the command buffer, so this must run after RenderDevice::Unlock.
	if (!initialized || shotDir.empty())
		return;

	if (shotInterval > 0.0 && engine->TotalTime >= nextShot)
	{
		nextShot = engine->TotalTime + shotInterval;
		char name[32];
		snprintf(name, sizeof(name), "shot%03d", shotIndex++);
		WriteShot(engine, device, name);
	}

	for (const std::string& name : pendingShots)
		WriteShot(engine, device, name);
	pendingShots.clear();
}
