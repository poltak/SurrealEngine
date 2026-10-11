
#include "Precomp.h"
#include "Harness.h"
#include "Engine.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Core/UClass.h"
#include "Native/NPlayerPawn.h"
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
	Mute = commandline->HasArg({}, "--mute");
	TraceKeys = commandline->HasArg({}, "--tracekeys");
	TraceAI = commandline->HasArg({}, "--traceai");
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
	if (engine->dxRootWindow && engine->dxRootWindow->IsCursorVisible())
		engine->dxRootWindow->SetRootCursorPos((float)(fx * engine->dxRootWindow->Width()), (float)(fy * engine->dxRootWindow->Height()));

	double scale = engine->window->GetDpiScale();
	return Point(fx * engine->window->GetPixelWidth() / scale, fy * engine->window->GetPixelHeight() / scale);
}

bool Harness::DropRealInput(const char* what)
{
	if (!initialized || events.empty() || injecting)
		return false;
	if (droppedRealEvents++ < 20)
		LogMessage(std::string("Harness: dropped real input event: ") + what);
	return true;
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
			player->SetLocation(vec3(x, y, z));
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
		// teleportto <actor name or class> [distance]: put the player near that actor, with a free line to it, and aim at it
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
			bool done = false;
			for (int step = 0; step < 16 && !done; step++)
			{
				float angle = step * (2.0f * 3.14159265f / 16.0f);
				vec3 spot = target->Location() + vec3(std::cos(angle), std::sin(angle), 0.0f) * distance;
				spot.z += std::max(player->CollisionHeight() - target->CollisionHeight(), 0.0f) + 2.0f;
				if (!player->SetLocation(spot))
					continue;
				vec3 eye = player->Location() + vec3(0.0f, 0.0f, player->BaseEyeHeight());
				if (engine->Level->Collision.TraceAnyHit(eye, target->Location(), player, false, true, true))
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
		// setprop <class> <property> <value>: set a property on each live actor of that class
		std::string className, propName, value;
		std::istringstream parts(e.Args);
		parts >> className >> propName;
		std::getline(parts, value);
		size_t first = value.find_first_not_of(" \t");
		value = first == std::string::npos ? std::string() : value.substr(first);
		int count = 0;
		for (UActor* actor : engine->Level->Actors)
		{
			if (actor && actor->IsA(className) && actor->HasProperty(propName))
			{
				actor->SetPropertyFromString(propName, value);
				count++;
			}
		}
		LogMessage("Harness: setprop changed " + std::to_string(count) + " actors");
	}
	else if (e.Command == "dump")
	{
		// dump <class> [radius] [property,property,...]
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
			if (!actor || !actor->IsA(className))
				continue;
			float dist = player ? length(actor->Location() - player->Location()) : 0.0f;
			if (radius > 0.0f && dist > radius)
				continue;
			std::string text = "Harness: dump " + actor->Name.ToString() + " state=" + actor->GetStateName().ToString();
			char buf[160];
			snprintf(buf, sizeof(buf), " loc=(%.0f %.0f %.0f) dist=%.0f lastRendered=%.1f", actor->Location().x, actor->Location().y, actor->Location().z, dist, engine->LevelInfo->TimeSeconds() - actor->LastRenderTime());
			text += buf;
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
			Rotator aim = Rotator::FromVector(aimTarget->Location() - eye);
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
