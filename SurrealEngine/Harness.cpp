
#include "Precomp.h"
#include "Harness.h"
#include "Render/RenderSubsystem.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include <thread>
#include <time.h>
#include <filesystem>
#include <chrono>
#include "Engine.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Extension/Windows/Text/UTextWindow.h"
#include "Packages/Extension/Windows/Text/UToggleWindow.h"
#include "Packages/Extension/Windows/UScaleWindow.h"
#include "Packages/Extension/Windows/TabGroup/UClipWindow.h"
#include "Packages/Extension/Windows/UListWindow.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Packages/Core/UClass.h"
#include "Native/NPlayerPawn.h"
#include "VM/NativeFunc.h"
#include "VM/Iterator.h"
#include "Math/coords.h"
#include "GC/GC.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Extension/UPlayerPawnExt.h"
#include "Packages/Extension/Flags/UFlagBase.h"
#include "Packages/Extension/Flags/UFlag.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "VM/ExpressionValue.h"
#include "VM/ScriptCall.h"
#include "Packages/Core/UFunction.h"
#include "Utils/Logger.h"
#include "Utils/File.h"
#include "VM/Frame.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

static FILE* harnessLogFile = nullptr;

#ifdef __APPLE__
#include <mach/mach.h>
static size_t ResidentMemoryMB()
{
	mach_task_basic_info_data_t info;
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
		return 0;
	return (size_t)(info.resident_size / (1024 * 1024));
}
#else
static size_t ResidentMemoryMB() { return 0; }
#endif


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
	Mute = commandline->HasArg({}, "--mute");
	TraceKeys = commandline->HasArg({}, "--tracekeys");
	TraceAI = commandline->HasArg({}, "--traceai");
	TracePath = commandline->GetArg({}, "--tracepath");
	CheckNatives = commandline->HasArg({}, "--checknatives");
	ShowStats = commandline->HasArg({}, "--stats");
	KeepLevelData = commandline->HasArg({}, "--keep-level-data");
	NoVSync = commandline->HasArg({}, "--novsync");
	double fixedFps = commandline->GetArgDouble({}, "--fixed-fps", 0.0);
	FixedDelta = fixedFps > 0.0 ? 1.0 / fixedFps : 0.0;
	Seed = commandline->GetArgInt({}, "--seed", -1);
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
	waitFile = commandline->GetArg({}, "--wait-file");

	std::string script = commandline->GetArg({}, "--script");
	if (!script.empty())
		LoadScript(script);
}

// The processor time of this thread in seconds. A frame time from this clock has no wait for the display in it.
static double ThreadCpuSeconds()
{
#if defined(__APPLE__) || defined(__linux__)
	timespec ts = {};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
		return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
	return 0.0;
}

bool Harness::GetWindowSize(int& width, int& height) const
{
	return !winSize.empty() && sscanf(winSize.c_str(), "%dx%d", &width, &height) == 2 && width > 0 && height > 0;
}

// A window manager can change the size of a new window some time after it opens. The size of the view changes what
// the game does, because some game logic depends on which actors the player sees. A test must give the same result
// in each run, so the game must not start before the window has its size. This waits in real time for the file of
// the test runner (--wait-file), and then until the window has kept the requested size for one second.
void Harness::SettleWindowSize(Engine* engine)
{
	int width = 0, height = 0;
	if (!engine->window || !GetWindowSize(width, height))
		return;

	using clock = std::chrono::steady_clock;
	const auto start = clock::now();
	auto correctSince = start;
	bool waitForFile = !waitFile.empty();
	while (clock::now() - start < std::chrono::seconds(20))
	{
		GameWindow::ProcessEvents();
		if (waitForFile && std::filesystem::exists(waitFile))
		{
			waitForFile = false;
			correctSince = clock::now();
		}
		if (engine->window->GetPixelWidth() != width || engine->window->GetPixelHeight() != height)
		{
			engine->window->SetResolution(winSize);
			correctSince = clock::now();
		}
		else if (!waitForFile && clock::now() - correctSince >= std::chrono::seconds(1))
		{
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	LogMessage(waitForFile ? "Harness: no file " + waitFile : "Harness: the window did not keep the size " + winSize);
}

void Harness::DrawOverlay(Engine* engine)
{
	if (shownTextures.empty() || !engine->render)
		return;

	// Cells of 76 canvas units, eight in a row, on a black background
	const float cell = 80.0f, size = 76.0f;
	UTexture* black = engine->DefaultTexture;
	int index = 0;
	for (UTexture* tex : shownTextures)
	{
		float x = 2.0f + (index % 8) * cell;
		float y = 2.0f + (index / 8) * cell;
		if (black)
			engine->render->DrawTile(black, x, y, size, size, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec4(0.0f), 0);
		engine->render->DrawTile(tex, x, y, size, size, 0.0f, 0.0f, (float)tex->USize(), (float)tex->VSize(), 1.0f, vec4(1.0f), vec4(0.0f), 0);
		index++;
	}
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
		// A comment starts with '#' at the start of the line or after a space. A map URL can have '#' in it.
		for (size_t comment = line.find('#'); comment != std::string::npos; comment = line.find('#', comment + 1))
		{
			if (comment == 0 || line[comment - 1] == ' ' || line[comment - 1] == '\t')
			{
				line = line.substr(0, comment);
				break;
			}
		}
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

	// The Deus Ex root window draws its own cursor and moves it with relative mouse input only.
	// Put that cursor at the same fraction of the root window.
	if (engine->dxRootWindow && engine->dxRootWindow->IsModalOpen())
		engine->dxRootWindow->SetRootCursorPos((float)(fx * engine->dxRootWindow->Width()), (float)(fy * engine->dxRootWindow->Height()));

	double scale = engine->window->GetDpiScale();
	return Point(fx * engine->window->GetPixelWidth() / scale, fy * engine->window->GetPixelHeight() / scale);
}

bool Harness::GetFakeGamepad(GamepadState& state) const
{
	if (!initialized || events.empty())
		return false;
	state = fakePad;
	state.Connected = fakePadUsed;
	state.Name = "test controller";
	return true;
}

static int ParsePadButton(const std::string& name)
{
	static const std::map<std::string, GamepadButton> names =
	{
		{ "a", GamepadButton::A }, { "b", GamepadButton::B }, { "x", GamepadButton::X }, { "y", GamepadButton::Y },
		{ "lb", GamepadButton::LeftShoulder }, { "rb", GamepadButton::RightShoulder },
		{ "back", GamepadButton::Back }, { "start", GamepadButton::Start },
		{ "l3", GamepadButton::LeftThumb }, { "r3", GamepadButton::RightThumb },
		{ "lt", GamepadButton::LeftTrigger }, { "rt", GamepadButton::RightTrigger },
		{ "up", GamepadButton::DPadUp }, { "down", GamepadButton::DPadDown },
		{ "left", GamepadButton::DPadLeft }, { "right", GamepadButton::DPadRight }
	};
	auto it = names.find(name);
	return it != names.end() ? (int)it->second : -1;
}

bool Harness::DropRealInput(const char* what)
{
	if (!initialized || events.empty() || injecting)
		return false;
	if (droppedRealEvents++ < 20)
		LogMessage(std::string("Harness: dropped real input event: ") + what);
	return true;
}

// The point of an actor that the player looks at. The location of a door is its hinge, which can be in a wall,
// so for a mover this is the middle of its brush.
static vec3 AimPoint(UActor* actor)
{
	UMover* mover = UObject::TryCast<UMover>(actor);
	if (!mover || !mover->Brush() || !mover->Brush()->Polys)
		return actor->Location();

	mat4 objectToWorld = mat4::translate(mover->Location()) * Coords::Rotation(mover->Rotation()).ToMatrix() * mat4::scale(mover->MainScale().Scale) * mat4::translate(-mover->PrePivot());
	vec3 low(0.0f), high(0.0f);
	bool first = true;
	for (const Poly& poly : mover->Brush()->Polys->Polys)
	{
		for (const vec3& vertex : poly.Vertices)
		{
			vec3 p = (objectToWorld * vec4(vertex, 1.0f)).xyz();
			if (first)
			{
				low = p;
				high = p;
				first = false;
			}
			else
			{
				low = vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
				high = vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
			}
		}
	}
	if (first)
		return actor->Location();
	return (low + high) * 0.5f;
}

void Harness::RunEvent(Engine* engine, const Event& e)
{
	LogMessage("Harness: " + e.Command + " " + e.Args);

	struct InjectScope
	{
		InjectScope(bool& flag) : flag(flag) { flag = true; }
		~InjectScope() { flag = false; }
		bool& flag;
	} injectScope(injecting);

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
	else if (e.Command == "pad")
	{
		std::istringstream args(e.Args);
		std::string action, what;
		args >> action >> what;
		fakePadUsed = true;
		if (action == "stick")
		{
			float x = 0.0f, y = 0.0f;
			args >> x >> y;
			(what == "left" ? fakePad.LeftX : fakePad.RightX) = x;
			(what == "left" ? fakePad.LeftY : fakePad.RightY) = y;
		}
		else if (action == "trigger")
		{
			float value = 0.0f;
			args >> value;
			(what == "left" ? fakePad.LeftTrigger : fakePad.RightTrigger) = value;
		}
		else if (action == "reset")
		{
			fakePad = {};
		}
		else
		{
			int button = ParsePadButton(what);
			bool down = action != "up";
			if (button < 0)
				LogMessage("Harness: unknown controller button: " + what);
			else if (button == (int)GamepadButton::LeftTrigger)
				fakePad.LeftTrigger = down ? 1.0f : 0.0f;
			else if (button == (int)GamepadButton::RightTrigger)
				fakePad.RightTrigger = down ? 1.0f : 0.0f;
			else
				fakePad.Buttons[button] = down;
			if (button >= 0 && action == "tap")
				events.insert(events.begin() + nextEvent, Event{ e.Time + 0.08, "pad", "up " + what });
		}
	}
	else if (e.Command == "char")
	{
		engine->OnWindowKeyChar(e.Args);
	}
	else if (e.Command == "oskey")
	{
		// oskey down|up|tap CODE [CHARS] | oskey flags MASK [CODE]: a key event with a key code of the window system,
		// sent through the window. This tests the key table of the window code, which the "key" command does not use.
		std::istringstream parts(e.Args);
		std::string kind, codeText, chars;
		parts >> kind >> codeText >> chars;
		int code = codeText.empty() ? 0 : (int)std::strtol(codeText.c_str(), nullptr, 0);
		if (kind == "down" || kind == "tap")
			engine->window->InjectNativeKeyEvent(0, code, 0, chars);
		else if (kind == "up")
			engine->window->InjectNativeKeyEvent(1, code, 0, chars);
		else if (kind == "flags")
			engine->window->InjectNativeKeyEvent(2, chars.empty() ? 0 : (int)std::strtol(chars.c_str(), nullptr, 0), (unsigned int)code, "");
		if (kind == "tap")
			events.insert(events.begin() + nextEvent, Event{ e.Time + 0.08, "oskey", "up " + codeText + " " + chars });
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
	else if (e.Command == "wheelup" || e.Command == "wheeldown")
	{
		// wheelup X Y | wheeldown X Y: one step of the mouse wheel with the mouse at that place
		Point pos = ParsePoint(engine, e.Args);
		engine->OnWindowMouseMove(pos);
		engine->OnWindowMouseWheel(pos, (e.Command == "wheelup") ? IK_MouseWheelUp : IK_MouseWheelDown);
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
		ExpressionValue found = ExpressionValue::BoolValue(false);
		std::string result = engine->ConsoleCommand(engine->viewport ? (UObject*)engine->viewport->Actor() : nullptr, e.Args, found.ToType<BitfieldBool&>());
		if (!found.ToBool())
			LogMessage("Harness: exec: command not found");
		if (!result.empty())
			LogMessage("Harness: exec result: " + result);
	}
	else if (e.Command == "teleport")
	{
		// teleport X Y Z [YAW] : move the player. YAW is in Unreal rotation units (65536 = full turn).
		float x = 0.0f, y = 0.0f, z = 0.0f;
		int yaw = 0;
		std::istringstream parts(e.Args);
		parts >> x >> y >> z;
		UPlayerPawn* player = engine->viewport ? UObject::TryCast<UPlayerPawn>(engine->viewport->Actor()) : nullptr;
		if (player)
		{
			if (!player->SetLocation(vec3(x, y, z)))
				LogMessage("Harness: teleport: no room for the player at that place");
			if (parts >> yaw)
			{
				Rotator r(0, yaw, 0);
				player->ViewRotation() = r;
				player->SetRotation(r);
			}
		}
	}
	else if (e.Command == "findfunc")
	{
		// findfunc <class> <function>: log which class holds the script function that the first actor of that class would call
		std::string className, funcName;
		std::istringstream parts(e.Args);
		parts >> className >> funcName;
		for (UActor* actor : engine->Level->Actors)
		{
			if (!actor || !actor->IsA(className))
				continue;
			UFunction* func = FindEventFunction(actor, funcName);
			std::string text = "Harness: findfunc " + actor->Name.ToString() + " (state " + actor->GetStateName().ToString() + ") " + funcName + " -> ";
			if (func)
			{
				std::string owner;
				for (UStruct* st = func->StructParent; st; st = st->StructParent)
					owner = st->Name.ToString() + (owner.empty() ? "" : "." + owner);
				text += owner + "." + func->Name.ToString() + " bytecode=" + std::to_string(func->BytecodeRaw.size()) + " native=" + std::to_string((int)AllFlags(func->FuncFlags, FunctionFlags::Native));
			}
			else
			{
				text += "none";
			}
			LogMessage(text);
			break;
		}
	}
	else if (e.Command == "teleportto")
	{
		// teleportto <actor name or class> [distance]: put the player near that actor, with a free line to it, and aim at it.
		// For a door or another mover, the place is near the middle of its brush.
		std::string targetName;
		float distance = 70.0f;
		std::istringstream parts(e.Args);
		parts >> targetName >> distance;
		UPlayerPawn* player = engine->viewport ? UObject::TryCast<UPlayerPawn>(engine->viewport->Actor()) : nullptr;
		UActor* target = nullptr;
		for (UActor* actor : engine->Level->Actors)
		{
			if (actor && actor != player && !actor->bDeleteMe() && actor->Name == NameString(targetName))
			{
				target = actor;
				break;
			}
		}
		if (!target)
		{
			float best = 0.0f;
			for (UActor* actor : engine->Level->Actors)
			{
				if (actor && actor != player && !actor->bDeleteMe() && actor->IsA(targetName))
				{
					float d = player ? length(actor->Location() - player->Location()) : 0.0f;
					if (!target || d < best)
					{
						target = actor;
						best = d;
					}
				}
			}
		}
		if (!player || !target)
		{
			LogMessage("Harness: teleportto: no actor " + targetName);
		}
		else
		{
			vec3 oldLocation = player->Location();
			vec3 targetPoint = AimPoint(target);
			bool isMover = UObject::TryCast<UMover>(target) != nullptr;
			bool done = false;
			for (int step = 0; step < 16 && !done; step++)
			{
				float angle = step * (2.0f * 3.14159265f / 16.0f);
				vec3 spot = targetPoint + vec3(std::cos(angle), std::sin(angle), 0.0f) * distance;
				spot.z += std::max(player->CollisionHeight() - target->CollisionHeight(), 0.0f) + 2.0f;
				if (!player->SetLocation(spot))
					continue;
				vec3 eye = player->Location() + vec3(0.0f, 0.0f, player->BaseEyeHeight());
				// The middle of a mover is inside its brush, so the free line ends before it
				vec3 lineEnd = isMover ? eye + (targetPoint - eye) * 0.8f : targetPoint;
				if (engine->Level->Collision.TraceAnyHit(eye, lineEnd, player, false, true, true))
					continue;
				player->Velocity() = vec3(0.0f);
				// The player can drop to the floor after this. Keep the aim on the target for a short time.
				aimTarget = target;
				aimUntil = engine->TotalTime + 0.5;
				done = true;
			}
			if (!done)
				player->SetLocation(oldLocation);
			char buf[200];
			snprintf(buf, sizeof(buf), "Harness: teleportto %s %s at (%.0f %.0f %.0f), player at (%.0f %.0f %.0f)", target->Name.ToString().c_str(), done ? "ok" : "FAILED",
				target->Location().x, target->Location().y, target->Location().z, player->Location().x, player->Location().y, player->Location().z);
			LogMessage(buf);
		}
	}
	else if (e.Command == "setprop")
	{
		// setprop <class> <property> <value>: set a property on each live actor of that class.
		// <property>[<index>] sets one element of a fixed-size array.
		std::string className, propName, value;
		std::istringstream parts(e.Args);
		parts >> className >> propName;
		std::getline(parts, value);
		size_t first = value.find_first_not_of(" \t");
		value = first == std::string::npos ? std::string() : value.substr(first);
		int index = 0;
		size_t bracket = propName.find('[');
		if (bracket != std::string::npos)
		{
			index = std::atoi(propName.c_str() + bracket + 1);
			propName = propName.substr(0, bracket);
		}
		int count = 0;
		for (UActor* actor : engine->Level->Actors)
		{
			if (actor && actor->IsA(className) && actor->HasProperty(propName))
			{
				UProperty* prop = actor->GetMemberProperty(propName);
				if (index < 0 || index >= prop->ArrayDimension)
					continue;
				prop->SetValueFromString(static_cast<uint8_t*>(actor->PropertyData.Ptr(prop)) + index * prop->ElementSize(), value);
				count++;
			}
		}
		LogMessage("Harness: setprop changed " + std::to_string(count) + " actors");
	}
	else if (e.Command == "dump")
	{
		// dump <class or actor name> [radius] [property,property,...]
		std::string className, extraProps;
		float radius = 0.0f;
		std::istringstream parts(e.Args);
		parts >> className >> radius >> extraProps;
		std::vector<std::string> props = { "Orders", "Alliance", "EnemyLastSeen", "SeekLevel", "bStasis" };
		if (!extraProps.empty())
		{
			props.clear();
			std::istringstream list(extraProps);
			std::string item;
			while (std::getline(list, item, ','))
				props.push_back(item);
		}
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		for (UActor* actor : engine->Level->Actors)
		{
			if (!actor || (!actor->IsA(className) && actor->Name != className))
				continue;
			float dist = player ? length(actor->Location() - player->Location()) : 0.0f;
			if (radius > 0.0f && dist > radius)
				continue;
			std::string text = "Harness: dump " + actor->Name.ToString() + " state=" + actor->GetStateName().ToString();
			char buf[160];
			snprintf(buf, sizeof(buf), " loc=(%.0f %.0f %.0f) dist=%.0f lastRendered=%.1f", actor->Location().x, actor->Location().y, actor->Location().z, dist, engine->LevelInfo->TimeSeconds() - actor->LastRenderTime());
			text += buf;
			if (engine->LaunchInfo.IsDeusEx() && actor->InStasis())
				text += " inStasis";
			if (UPawn* pawn = UObject::TryCast<UPawn>(actor))
			{
				text += " health=" + std::to_string(pawn->Health());
				if (pawn->Enemy())
					text += " enemy=" + pawn->Enemy()->Name.ToString();
			}
			for (const std::string& prop : props)
			{
				if (!actor->HasProperty(prop))
					continue;
				std::string value = actor->GetPropertyAsString(prop);

				// A fixed-size array: show all elements
				UProperty* member = actor->GetMemberProperty(prop);
				if (member && member->ArrayDimension > 1)
				{
					value = "(";
					for (int i = 0; i < member->ArrayDimension; i++)
						value += (i > 0 ? "," : "") + member->PrintValue(member->GetElement(actor->PropertyData.Ptr(member), i));
					value += ")";
				}
				text += " " + prop + "=" + value;
			}
			LogMessage(text);
		}
	}
	else if (e.Command == "travel")
	{
		// travel <url>: send the player to a map as a map exit does it (a relative travel with items)
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		if (player && !e.Args.empty())
			NPlayerPawn::ClientTravel(player, e.Args, (uint8_t)ETravelType::TRAVEL_Relative, true);
	}
	else if (e.Command == "clearcurrent")
	{
		// clearcurrent: remove the map states of the current game (Save/Current), as a new game does it.
		// A test that starts a map directly and then travels uses this, so that it does not get the map states of an older run.
		if (engine->LaunchInfo.IsDeusEx())
			engine->DXDeleteSaveGameFiles(engine->DXSaveDirectory(-2));
	}
	else if (e.Command == "mem")
	{
		// mem: log the memory that the process uses and the number of objects
		size_t objects = 0;
		for (GCObject* obj : GC::GetObjects())
		{
			(void)obj;
			objects++;
		}
		// The objects of the current level and the size of their property data, and the lightmap cache of the level
		size_t levelObjects = 0, levelProps = 0, allProps = 0;
		for (GCObject* gcobj : GC::GetObjects())
		{
			UObject* uobj = dynamic_cast<UObject*>(gcobj);
			if (!uobj)
				continue;
			allProps += uobj->PropertyData.Size;
			if (uobj->package == engine->LevelPackage)
			{
				levelObjects++;
				levelProps += uobj->PropertyData.Size;
			}
		}
		size_t lightmaps = engine->Level ? engine->Level->Light.GetMemoryUsage() : 0;
		LogMessage("Harness: mem resident=" + std::to_string(ResidentMemoryMB()) + " MB objects=" + std::to_string(objects) + " actors=" + std::to_string(engine->Level ? engine->Level->Actors.size() : 0) +
			" levelobjects=" + std::to_string(levelObjects) + " levelprops=" + std::to_string(levelProps / 1024) + " KB allprops=" + std::to_string(allProps / (1024 * 1024)) + " MB lightmaps=" + std::to_string(lightmaps / 1024) + " KB");
	}
	else if (e.Command == "perf")
	{
		// perf: log the mean frame time in real time and the script work for each frame since the last "perf" command
		// "ms" is real time. A window that the display shows cannot get frames faster than the display takes them,
		// so "ms" has a lower limit then. "cpums" is the processor time of the main thread and has no such limit.
		double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		double cpuNow = ThreadCpuSeconds();
		uint64_t frames = perfFrames - perfFramesAtReport;
		if (perfTimeAtReport != 0.0 && frames > 0)
		{
			double ms = (now - perfTimeAtReport) * 1000.0 / frames;
			double cpums = (cpuNow - perfCpuAtReport) * 1000.0 / frames;
			char text[256];
			snprintf(text, sizeof(text), "Harness: perf frames=%llu ms=%.2f cpums=%.2f fps=%.1f scriptcalls=%llu nativecalls=%llu statements=%llu (each frame)",
				(unsigned long long)frames, ms, cpums, 1000.0 / ms,
				(unsigned long long)((Frame::ScriptCallCount - perfScriptCalls) / frames),
				(unsigned long long)((Frame::NativeCallCount - perfNativeCalls) / frames),
				(unsigned long long)((Frame::StatementCount - perfStatements) / frames));
			LogMessage(text);
			// The totals are the same in each run of the same test, so a difference shows a change of behavior
			snprintf(text, sizeof(text), "Harness: perf totals scriptcalls=%llu nativecalls=%llu statements=%llu",
				(unsigned long long)Frame::ScriptCallCount, (unsigned long long)Frame::NativeCallCount, (unsigned long long)Frame::StatementCount);
			LogMessage(text);
		}
		perfTimeAtReport = now;
		perfCpuAtReport = cpuNow;
		perfFramesAtReport = perfFrames;
		perfScriptCalls = Frame::ScriptCallCount;
		perfNativeCalls = Frame::NativeCallCount;
		perfStatements = Frame::StatementCount;
	}
	else if (e.Command == "windows")
	{
		// windows: log each visible window of the Deus Ex interface: class, place in its parent, size, and flags.
		// "focus" marks the window that gets the keys, "mouse" the window under the mouse.
		// A text window also has the start of its text, a list window its count of rows, and a toggle or checkbox its state.
		// A scale has its thumb position, its count of positions and the span of its thumb. A clip window has the
		// position of its child in units.
		URootWindow* root = engine->dxRootWindow;
		if (!root)
		{
			LogMessage("Harness: windows: no root window");
		}
		else
		{
			struct Dump
			{
				static void Window(URootWindow* root, UWindow* window, int depth)
				{
					if (!window->bIsVisible())
						return;
					// A text window also shows the start of its text, and a list window its count of rows
					std::string content;
					if (auto textWindow = UObject::TryCast<UTextWindow>(window))
					{
						std::string text = textWindow->Text().substr(0, 48);
						for (char& c : text)
						{
							if (c == '\n' || c == '\r')
								c = ' ';
						}
						if (!text.empty())
							content = " text=\"" + text + "\"";
					}
					else if (auto listWindow = UObject::TryCast<UListWindow>(window))
					{
						content = " rows=" + std::to_string(listWindow->GetNumRows());
					}
					if (auto toggleWindow = UObject::TryCast<UToggleWindow>(window))
						content += toggleWindow->GetToggle() ? " toggle=on" : " toggle=off";
					if (auto scaleWindow = UObject::TryCast<UScaleWindow>(window))
					{
						content += " tick=" + std::to_string(scaleWindow->GetTickPosition()) + "/" + std::to_string(scaleWindow->GetNumTicks());
						if (scaleWindow->GetThumbSpan() > 0)
							content += " span=" + std::to_string(scaleWindow->GetThumbSpan());
					}
					if (auto clipWindow = UObject::TryCast<UClipWindow>(window))
					{
						int column = 0, row = 0;
						clipWindow->GetChildPosition(column, row);
						content += " child=" + std::to_string(column) + "," + std::to_string(row);
					}

					char buf[500];
					snprintf(buf, sizeof(buf), "Harness: window %*s%s x=%d y=%d w=%d h=%d%s%s%s%s%s", depth * 2, "", UObject::GetUClassName(window).ToString().c_str(),
						(int)window->UsedX, (int)window->UsedY, (int)window->Width(), (int)window->Height(),
						window->bIsSelectable() ? " selectable" : "", window->bIsSensitive() ? "" : " not-sensitive",
						window == root->FocusWindow() ? " focus" : "", window == root->lastMouseWindow() ? " mouse" : "", content.c_str());
					LogMessage(buf);
					for (UWindow* child = window->lastChild(); child; child = child->prevSibling())
						Window(root, child, depth + 1);
				}
			};
			Dump::Window(root, root, 0);
		}
	}
	else if (e.Command == "visible")
	{
		// visible: log what the last frame showed: the count of frames (the view of the camera, and each sky and
		// mirror in it), of level surfaces and of actors. The "--stats" option shows the same numbers on the screen.
		if (engine->render)
		{
			const auto& stats = engine->render->Stats;
			LogMessage("Harness: visible frames=" + std::to_string(stats.Frames) + " surfaces=" + std::to_string(stats.Surfaces) + " actors=" + std::to_string(stats.Actors));
		}
	}
	else if (e.Command == "texts")
	{
		// texts: log each line of text that the interface draws in the frame that comes now
		captureTextFrames = 1;
	}
	else if (e.Command == "cursor")
	{
		// cursor: log the mouse cursor that the interface shows: the texture, its hot spot, its color, its shadow
		// texture and the class of the window under the mouse
		URootWindow* root = engine->dxRootWindow;
		if (!root)
		{
			LogMessage("Harness: cursor: no root window");
		}
		else if (!root->IsCursorVisible())
		{
			LogMessage("Harness: cursor hidden");
		}
		else
		{
			float relativeX = 0.0f, relativeY = 0.0f, hotX = 0.0f, hotY = 0.0f;
			Color color = { 255, 255, 255, 255 };
			UTexture* shadow = nullptr;
			UWindow* focus = root->GetCursorFocus(relativeX, relativeY);
			UTexture* cursor = focus->QueryCursor(relativeX, relativeY, hotX, hotY, color, shadow);
			char buf[400];
			snprintf(buf, sizeof(buf), "Harness: cursor %s hot=%d,%d color=%d,%d,%d shadow=%s window=%s",
				cursor ? cursor->Name.ToString().c_str() : "none", (int)hotX, (int)hotY, color.R, color.G, color.B,
				shadow ? shadow->Name.ToString().c_str() : "none", UObject::GetUClassName(focus).ToString().c_str());
			LogMessage(buf);
		}
	}
	else if (e.Command == "showtex")
	{
		// showtex PKG.NAME ...: find each texture in its package. A fire texture then shows how its sparks look.
		shownTextures.clear();
		std::istringstream names(e.Args);
		std::string fullName;
		while (names >> fullName)
		{
			size_t dot = fullName.find('.');
			if (dot == std::string::npos)
				continue;
			UTexture* tex = nullptr;
			try
			{
				Package* package = engine->packages->GetPackage(fullName.substr(0, dot));
				for (const char* className : { "FireTexture", "Texture", "WaveTexture", "WetTexture", "IceTexture", "ScriptedTexture" })
				{
					if (!tex && package)
						tex = UObject::TryCast<UTexture>(package->GetUObject(className, fullName.substr(dot + 1)));
				}
			}
			catch (const std::exception&)
			{
			}
			if (tex)
				shownTextures.push_back(tex);
			else
				LogMessage("Harness: showtex: no texture " + fullName);
		}
	}
	else if (e.Command == "surfaces")
	{
		// surfaces GROUP [COUNT]: log the level surfaces that have a texture of that group, with their middle point,
		// their normal and their radius (default 20 surfaces). The group "Ladder" gives the ladders of a map.
		std::istringstream parts(e.Args);
		std::string group;
		int maxCount = 20;
		parts >> group >> maxCount;
		ULevel* level = engine->Level;
		int count = 0;
		if (level && level->Model && !group.empty())
		{
			NameString groupName(group);
			for (const BspSurface& surf : level->Model->Surfaces)
			{
				if (!surf.Material || !surf.Material->Outer() || surf.Material->Outer()->Name != groupName)
					continue;
				if (count++ >= maxCount)
					continue;
				vec3 normal(0.0f);
				if (surf.vNormal >= 0 && (size_t)surf.vNormal < level->Model->Vectors.size())
					normal = level->Model->Vectors[surf.vNormal];
				char buf[300];
				snprintf(buf, sizeof(buf), "Harness: surface %s center=(%.0f %.0f %.0f) normal=(%.2f %.2f %.2f) radius=%.0f",
					surf.Material->Name.ToString().c_str(), surf.Center.x, surf.Center.y, surf.Center.z, normal.x, normal.y, normal.z, surf.Radius);
				LogMessage(buf);
			}
		}
		LogMessage("Harness: surfaces " + group + " count=" + std::to_string(count));
	}
	else if (e.Command == "tracetex")
	{
		// tracetex [YAWOFFSET] [PITCHOFFSET]: trace from the eye of the player and log what the line hits (actor, surface texture)
		int yawOffset = 0, pitchOffset = 0;
		std::istringstream parts(e.Args);
		parts >> yawOffset >> pitchOffset;
		UPawn* player = engine->viewport ? UObject::TryCast<UPawn>(engine->viewport->Actor()) : nullptr;
		if (player)
		{
			vec3 start = player->Location();
			start.z += player->EyeHeight();
			Rotator rot = player->ViewRotation();
			rot.Yaw += yawOffset;
			rot.Pitch += pitchOffset;
			vec3 X, Y, Z;
			Coords::Rotation(rot).GetAxes(X, Y, Z);
			auto hits = CollectMultiLineCheckHits(player, start + X * 4000.0f, start, vec3(0.0f), true);
			std::string text = "Harness: tracetex " + std::to_string(yawOffset) + " " + std::to_string(pitchOffset) + ":";
			for (const TraceHitInfo& hit : hits)
			{
				char buf[256];
				snprintf(buf, sizeof(buf), " [%s tex=%s group=%s flags=0x%x dist=%.0f]", hit.Actor ? hit.Actor->Name.ToString().c_str() : "none", hit.TexName.ToString().c_str(), hit.TexGroup.ToString().c_str(), hit.Flags, length(hit.HitLoc - start));
				text += buf;
			}
			LogMessage(text);
		}
	}
	else if (e.Command == "flags")
	{
		// flags [text]: log the number of flags of the player, and each flag whose name contains the text
		auto player = engine->viewport ? UObject::TryCast<UPlayerPawnExt>(engine->viewport->Actor()) : nullptr;
		UFlagBase* flagBase = player ? player->FlagBase() : nullptr;
		if (!flagBase)
		{
			LogMessage("Harness: flags: the player has no flag base");
		}
		else
		{
			int count = 0;
			std::string list;
			auto table = flagBase->hashTable();
			for (int i = 0; i < UFlagBase::HashTableSize; i++)
			{
				for (UFlag* flag = table[i]; flag; flag = flag->nextFlag())
				{
					count++;
					std::string name = flag->FlagName().ToString();
					if (e.Args.empty() || name.find(e.Args) == std::string::npos)
						continue;
					list += " " + name;
					if (flag->flagType() == (uint8_t)EFlagType::Bool)
						list += flagBase->GetBool(flag->FlagName()) ? "=True" : "=False";
					list += "(exp " + std::to_string(flag->expiration()) + ")";
				}
			}
			LogMessage("Harness: flags count=" + std::to_string(count) + list);
		}
	}
	else if (e.Command == "setflag")
	{
		// setflag <name> <true|false>: set a bool flag of the player
		std::string name, value;
		std::istringstream parts(e.Args);
		parts >> name >> value;
		auto player = engine->viewport ? UObject::TryCast<UPlayerPawnExt>(engine->viewport->Actor()) : nullptr;
		if (player && player->FlagBase() && !name.empty())
		{
			player->FlagBase()->SetBool(name, value != "false" && value != "False" && value != "0", true, 0);
			LogMessage("Harness: setflag " + name);
		}
	}
	else if (e.Command == "goals")
	{
		// goals: log the goals of the player (Deus Ex)
		UObject* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		UObject* goal = (player && player->HasProperty("FirstGoal")) ? player->GetUObject("FirstGoal") : nullptr;
		int count = 0;
		for (; goal && count < 100; goal = goal->GetUObject("next"), count++)
		{
			LogMessage("Harness: goal " + goal->GetPropertyAsString("goalName") + " primary=" + goal->GetPropertyAsString("bPrimaryGoal") +
				" completed=" + goal->GetPropertyAsString("bCompleted") + " text=\"" + goal->GetString("text").substr(0, 60) + "\"");
		}
		LogMessage("Harness: goals count=" + std::to_string(count));
	}
	else if (e.Command == "trigger")
	{
		// trigger <tag>: send the Trigger event to each actor with that tag, as a trigger or a conversation does it.
		// The player is the other actor and the instigator.
		std::string tagName;
		std::istringstream parts(e.Args);
		parts >> tagName;
		NameString tag(tagName);
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		int count = 0;
		size_t actorCount = engine->Level->Actors.size();
		for (size_t i = 0; i < actorCount && !tagName.empty(); i++)
		{
			UActor* actor = engine->Level->Actors[i];
			if (actor && !actor->bDeleteMe() && actor->Tag() == tag)
			{
				CallEvent(actor, EventName::Trigger, { ExpressionValue::ObjectValue(player), ExpressionValue::ObjectValue(player) });
				count++;
			}
		}
		LogMessage("Harness: trigger " + tagName + ": " + std::to_string(count) + " actors");
	}
	else if (e.Command == "inv")
	{
		// inv: log the inventory chain of the player
		UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
		std::string list;
		int count = 0;
		for (UInventory* item = player ? player->Inventory() : nullptr; item && count < 200; item = item->Inventory())
		{
			list += " " + item->Class->Name.ToString();
			count++;
		}
		LogMessage("Harness: inv count=" + std::to_string(count) + list);
	}
	else if (e.Command == "sounds")
	{
		// sounds: log the sounds that play, with the actor, the sound and the slot (5 is the talk slot of a speech)
		Array<std::string> lines = engine->audiodev ? engine->audiodev->DescribeSounds() : Array<std::string>();
		LogMessage("Harness: sounds count=" + std::to_string(lines.size()));
		for (const std::string& line : lines)
			LogMessage("Harness: sound " + line);
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

// Loads the script packages of the game and logs each native function that the engine has no handler for.
// A script call to such a function stops the engine with "Unknown native function".
static void CheckNativeFunctions(Engine* engine)
{
	for (const char* name : { "Core", "Engine", "Extension", "DeusEx", "ConSys", "DeusExText", "DeusExUI", "IpDrv", "DeusExCharacters", "DeusExDeco", "DeusExItems" })
	{
		try
		{
			if (Package* package = engine->packages->GetPackage(name))
				package->LoadAll();
		}
		catch (const std::exception& e)
		{
			LogMessage(std::string("Harness: checknatives: could not load ") + name + ": " + e.what());
		}
	}

	std::vector<std::string> missing;
	int total = 0;
	for (GCObject* gcobj : GC::GetObjects())
	{
		UFunction* func = dynamic_cast<UFunction*>(gcobj);
		if (!func || !AllFlags(func->FuncFlags, FunctionFlags::Native) || !func->NativeStruct)
			continue;
		total++;

		bool found = false;
		if (func->NativeFuncIndex != 0)
			found = (size_t)func->NativeFuncIndex < NativeFunctions::NativeByIndex.size() && NativeFunctions::NativeByIndex[func->NativeFuncIndex];
		else
			found = (bool)NativeFunctions::NativeByName[{ func->Name, func->NativeStruct->Name }];

		if (!found)
			missing.push_back(func->NativeStruct->Name.ToString() + "." + func->Name.ToString() + " (" + std::to_string(func->NativeFuncIndex) + ")");
	}

	std::sort(missing.begin(), missing.end());
	missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
	LogMessage("Harness: checknatives: " + std::to_string(total) + " native functions, " + std::to_string(missing.size()) + " without a handler");
	for (const std::string& name : missing)
		LogMessage("Harness: checknatives missing: " + name);
}

void Harness::LogDrawnText(float x, float y, const std::string& text)
{
	std::string shown = text.substr(0, 60);
	for (char& c : shown)
	{
		if (c == '\n' || c == '\r')
			c = ' ';
	}
	char buf[200];
	snprintf(buf, sizeof(buf), "Harness: text x=%d y=%d \"%s\"", (int)std::lround(x), (int)std::lround(y), shown.c_str());
	LogMessage(buf);
}

void Harness::Tick(Engine* engine)
{
	if (!initialized)
		return;

	perfFrames++;

	// The texts command logs the text of one frame: the frame that comes after the command
	if (captureTextFrames > 0)
		captureTextFrames--;

	if (CheckNatives)
	{
		CheckNatives = false;
		CheckNativeFunctions(engine);
		engine->quit = true;
		return;
	}

	// A tiling window manager can resize the window after it opens. Put the requested size back.
	if (!winSize.empty() && engine->TotalTime >= nextWinSizeCheck)
	{
		nextWinSizeCheck = engine->TotalTime + 1.0;
		int w = 0, h = 0;
		if (sscanf(winSize.c_str(), "%dx%d", &w, &h) == 2 && (engine->window->GetPixelWidth() != w || engine->window->GetPixelHeight() != h))
			engine->window->SetResolution(winSize);
	}

	if (aimTarget)
	{
		UPlayerPawn* player = engine->viewport ? UObject::TryCast<UPlayerPawn>(engine->viewport->Actor()) : nullptr;
		if (!player || aimTarget->bDeleteMe() || engine->TotalTime > aimUntil)
		{
			aimTarget = nullptr;
		}
		else
		{
			vec3 eye = player->Location() + vec3(0.0f, 0.0f, player->EyeHeight());
			Rotator aim = Rotator::FromVector(AimPoint(aimTarget) - eye);
			player->ViewRotation() = Rotator(aim.Pitch & 0xffff, aim.Yaw & 0xffff, 0);
			player->SetRotation(Rotator(0, aim.Yaw & 0xffff, 0));
		}
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
