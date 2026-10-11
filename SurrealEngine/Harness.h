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
//   --novsync               do not wait for the display after each frame
//   --winsize=<W>x<H>       open the window with this size in pixels and keep it. The game starts when the
//                           window manager has stopped changing the size of the new window.
//   --wait-file=<path>      with --winsize: do not start the game before this file is there. The test runner makes
//                           the file when it has told the window manager what to do with the new window.
//   --fixed-fps=<N>         advance the game by 1/N seconds per frame (same result at any render speed)
//   --seed=<N>              seed of the random number generator
//   --script=<path>         timed input script, one event per line: "<seconds> <command> [args]"
//
// Script commands (X and Y are fractions of the window, 0..1):
//   keydown K | keyup K | key K     K is a key name (a-z, 0-9, enter, esc, space, f1..f12, ...) or a key code
//   char TEXT                       typed characters
//   move X Y | click X Y | rclick X Y | mousedown X Y | mouseup X Y
//   wheelup X Y | wheeldown X Y     one step of the mouse wheel with the mouse at that place
//   raw DX DY                       relative mouse movement
//   exec COMMAND                    console command
//   shot NAME                       write one frame now as NAME.bmp
//   teleportto NAME [DISTANCE]      put the player near the actor with that name (or the nearest of that class) and aim at it
//   teleport X Y Z [YAW]            move the player (YAW in Unreal units, 65536 = full turn)
//   setprop CLASS PROPERTY VALUE    set a property on each live actor of that class (PROPERTY[N] for an array element)
//   dump CLASS [RADIUS] [PROPS]     log state and location of each actor of that class, with the listed properties (a,b,c)
//   showtex PKG.NAME [PKG.NAME ...] draw these textures in a grid over the picture in each frame (no names: stop)
//   windows                         log the tree of the visible Deus Ex interface windows with place, size,
//                                   the start of the text of a text window, the row count of a list and the
//                                   state of a toggle or checkbox, the thumb position of a scale, and the
//                                   child position of a clip window
//   log TEXT | quit
class Harness
{
public:
	static Harness& Get();

	void Init();
	void LogFatal(const std::string& text);

	// --winsize: the requested size of the window in pixels. False if the run has no fixed window size.
	bool GetWindowSize(int& width, int& height) const;

	// Wait, before the game starts, until the new window keeps the requested size
	void SettleWindowSize(Engine* engine);
	void Tick(Engine* engine);
	void AfterFrame(Engine* engine, RenderDevice* device);

	// Draw the textures of the "showtex" command. The render code calls this at the end of a frame.
	void DrawOverlay(Engine* engine);

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
	bool CheckNatives = false; // --checknatives: log each native script function that has no handler, then quit
	bool ShowStats = false;  // --stats: show the frame rate and object counts on the screen (same as the console command "timedemo 1")
	bool KeepLevelData = false; // --keep-level-data: do not free the lightmaps and the geometry of a level that the engine leaves
	bool NoVSync = false;    // --novsync: do not wait for the display after each frame. Then a frame time is only the work of the engine.

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
	std::vector<class UTexture*> shownTextures;
	std::string winSize;
	std::string waitFile;
	double nextWinSizeCheck = 1.5; // not in the first frames: the window manager can resize the new window at that time
	std::vector<Event> events;
	size_t nextEvent = 0;

	// State of the "perf" command: the values at the last report
	uint64_t perfFrames = 0;
	uint64_t perfFramesAtReport = 0;
	double perfTimeAtReport = 0.0;
	double perfCpuAtReport = 0.0;
	uint64_t perfScriptCalls = 0;
	uint64_t perfNativeCalls = 0;
	uint64_t perfStatements = 0;
};
