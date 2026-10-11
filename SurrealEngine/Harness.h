#pragma once

#include <string>
#include <vector>

class Engine;
class RenderDevice;
class UActor;

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
//   --mute                  no sound output
//   --winsize=<W>x<H>       keep the window at this size in pixels
//   --fixed-fps=<N>         advance the game by 1/N seconds per frame (same result at any render speed)
//   --seed=<N>              seed of the random number generator
//   --script=<path>         timed input script, one event per line: "<seconds> <command> [args]"
//
// Script commands (X and Y are fractions of the window, 0..1):
//   keydown K | keyup K | key K     K is a key name (a-z, 0-9, enter, esc, space, f1..f12, ...) or a key code
//   char TEXT                       typed characters
//   move X Y | click X Y | rclick X Y | mousedown X Y | mouseup X Y
//   raw DX DY                       relative mouse movement
//   exec COMMAND                    console command
//   shot NAME                       write one frame now as NAME.bmp
//   teleportto NAME [DISTANCE]      put the player near the actor with that name (or the nearest of that class) and aim at it
//   teleport X Y Z [YAW]            move the player (YAW in Unreal units, 65536 = full turn)
//   setprop CLASS PROPERTY VALUE    set a property on each live actor of that class
//   dump CLASS [RADIUS] [PROPS]     log state and location of each actor of that class, with the listed properties (a,b,c)
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
	double FixedDelta = 0.0; // --fixed-fps=<N>: every frame advances the game by 1/N seconds, at any render speed
	int Seed = -1;           // --seed=<N>: seed of the random number generator, for runs that repeat exactly
	bool Mute = false;       // --mute: no sound output
	bool TraceAI = false;    // --traceai: log each step of AICanSee for a player target
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
	UActor* aimTarget = nullptr;
	double aimUntil = 0.0;
	std::string winSize;
	double nextWinSizeCheck = 1.5; // not in the first frames: the window manager can resize the new window at that time
	std::vector<Event> events;
	size_t nextEvent = 0;
};
