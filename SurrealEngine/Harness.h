#pragma once

#include <string>
#include <vector>

class Engine;
class RenderDevice;

// Test harness of the deus-ex-mac fork. It lets a script drive the game with no person at the keyboard.
//
// Command-line options:
//   --nolauncher            start the first game folder from the command line, no launcher window
//   --logfile=<path>        write each log line to this file as it happens
//   --exit-after=<seconds>  stop the engine after this run time
//   --shots=<dir>           write frames as 32 bit BMP files into this folder
//   --shot-interval=<s>     time between automatic frames (0 = no automatic frames)
//   --shot-first=<s>        time of the first automatic frame
//   --nograb                keep the mouse cursor free
//   --winsize=<W>x<H>       keep the window at this size in pixels
//   --script=<path>         timed input script, one event per line: "<seconds> <command> [args]"
//
// Script commands (X and Y are fractions of the window, 0..1):
//   keydown K | keyup K | key K     K is a key name (a-z, 0-9, enter, esc, space, f1..f12, ...) or a key code
//   char TEXT                       typed characters
//   move X Y | click X Y | rclick X Y | mousedown X Y | mouseup X Y
//   raw DX DY                       relative mouse movement
//   exec COMMAND                    console command
//   shot NAME                       write one frame now as NAME.bmp
//   log TEXT | quit
class Harness
{
public:
	static Harness& Get();

	void Init();
	void LogFatal(const std::string& text);
	void Tick(Engine* engine);
	void AfterFrame(Engine* engine, RenderDevice* device);

	// True for a window event that comes from the real keyboard or mouse while a script drives the game.
	// The engine drops such events, so a person who works on the same computer cannot change a test.
	bool DropRealInput(const char* what);

	bool NoLauncher = false;
	bool NoGrab = false;
	bool TraceKeys = false; // --tracekeys: log how the Deus Ex window system routes each key event

private:
	struct Event
	{
		double Time = 0.0;
		std::string Command;
		std::string Args;
	};

	void LoadScript(const std::string& filename);
	void RunEvent(Engine* engine, const Event& e);
	void WriteShot(Engine* engine, RenderDevice* device, const std::string& name);

	bool initialized = false;
	bool injecting = false;
	int droppedRealEvents = 0;
	double exitAfter = 0.0;
	std::string shotDir;
	double shotInterval = 0.0;
	double nextShot = 0.0;
	int shotIndex = 0;
	std::vector<std::string> pendingShots;
	std::string winSize;
	double nextWinSizeCheck = 0.0;
	std::vector<Event> events;
	size_t nextEvent = 0;
};
