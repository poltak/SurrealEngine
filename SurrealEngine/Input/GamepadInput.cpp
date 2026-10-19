#include "Precomp.h"
#include "Input/GamepadInput.h"
#include "Engine.h"
#include "Harness.h"
#include "Package/PackageManager.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include <cmath>

// The settings and the version of the default bindings are in this section of the user ini
static const char* SettingsSection = "SurrealEngine.Gamepad";
static const char* BindingsVersion = "1";

// The default bindings for Deus Ex. The second layer has the augmentations in the order of their F keys: the D-pad
// clockwise from up for F3 to F6, then Y, B, A and X for F7 to F10, and RB for F11. R3 alone gives F12, the light.
static const std::pair<const char*, const char*> DefaultBindings[] =
{
	{ "JoyX", "Axis aStrafe Speed=300" },       // left stick
	{ "JoyY", "Axis aBaseY Speed=300" },
	{ "JoyZ", "" },
	{ "JoyR", "" },
	{ "JoyU", "Axis aTurn Speed=280" },         // right stick
	{ "JoyV", "Axis aLookUp Speed=200" },
	{ "Joy1", "Jump" },                         // A
	{ "Joy2", "Duck" },                         // B
	{ "Joy3", "ReloadWeapon" },                 // X
	{ "Joy4", "SwitchAmmo" },                   // Y
	{ "Joy5", "" },                             // LB holds the second layer
	{ "Joy6", "ToggleScope" },                  // RB
	{ "Joy7", "ShowInventoryWindow" },          // Back
	{ "Joy8", "ShowMainMenu" },                 // Start
	{ "Joy9", "ToggleWalk" },                   // L3
	{ "Joy10", "ActivateAugmentation 9" },      // R3
	{ "Joy11", "ParseRightClick" },             // LT
	{ "Joy12", "ParseLeftClick|Fire" },         // RT
	{ "Joy13", "" },
	{ "Joy14", "" },
	{ "Joy15", "" },
	{ "Joy16", "" },
	{ "JoyPovUp", "ShowGoalsWindow" },
	{ "JoyPovDown", "PutInHand" },
	{ "JoyPovLeft", "PrevBeltItem" },
	{ "JoyPovRight", "NextBeltItem" },
	{ "LayerJoyPovUp", "ActivateAugmentation 0" },
	{ "LayerJoyPovRight", "ActivateAugmentation 1" },
	{ "LayerJoyPovDown", "ActivateAugmentation 2" },
	{ "LayerJoyPovLeft", "ActivateAugmentation 3" },
	{ "LayerJoy4", "ActivateAugmentation 4" },
	{ "LayerJoy2", "ActivateAugmentation 5" },
	{ "LayerJoy1", "ActivateAugmentation 6" },
	{ "LayerJoy3", "ActivateAugmentation 7" },
	{ "LayerJoy6", "ActivateAugmentation 8" },
	{ "LayerJoy7", "QuickLoad" },
	{ "LayerJoy8", "QuickSave" },
	{ "LayerJoy9", "DropItem" },
	{ "LayerJoy10", "ToggleLaser" },
	{ "LayerJoy11", "LeanLeft" },
	{ "LayerJoy12", "LeanRight" },
};

static const char* ModeName[] = { "game", "menu", "key capture" };

static EInputKey GameKey(GamepadButton button)
{
	switch (button)
	{
	case GamepadButton::A: return IK_Joy1;
	case GamepadButton::B: return IK_Joy2;
	case GamepadButton::X: return IK_Joy3;
	case GamepadButton::Y: return IK_Joy4;
	case GamepadButton::LeftShoulder: return IK_Joy5;
	case GamepadButton::RightShoulder: return IK_Joy6;
	case GamepadButton::Back: return IK_Joy7;
	case GamepadButton::Start: return IK_Joy8;
	case GamepadButton::LeftThumb: return IK_Joy9;
	case GamepadButton::RightThumb: return IK_Joy10;
	case GamepadButton::LeftTrigger: return IK_Joy11;
	case GamepadButton::RightTrigger: return IK_Joy12;
	case GamepadButton::DPadUp: return IK_JoyPovUp;
	case GamepadButton::DPadDown: return IK_JoyPovDown;
	case GamepadButton::DPadLeft: return IK_JoyPovLeft;
	case GamepadButton::DPadRight: return IK_JoyPovRight;
	default: return IK_None;
	}
}

static bool IsDPad(GamepadButton button)
{
	return button == GamepadButton::DPadUp || button == GamepadButton::DPadDown || button == GamepadButton::DPadLeft || button == GamepadButton::DPadRight;
}

// A round dead zone: the stick has no effect near the middle, and the rest of its way goes from 0 to 1. The curve
// makes small movements smaller, for exact aim. The direction does not change.
static void ApplyDeadZone(float x, float y, float deadZone, float curve, float& outX, float& outY)
{
	float length = std::sqrt(x * x + y * y);
	if (length <= deadZone || length <= 0.0f)
	{
		outX = 0.0f;
		outY = 0.0f;
		return;
	}
	float scaled = std::min((length - deadZone) / (1.0f - deadZone), 1.0f);
	scaled = std::pow(scaled, curve);
	outX = x / length * scaled;
	outY = y / length * scaled;
}

GamepadInput::GamepadInput()
{
}

GamepadInput::~GamepadInput()
{
}

bool GamepadInput::IsLayerKey(EInputKey key)
{
	return (key >= IK_Joy1 && key <= IK_Joy16) || (key >= IK_JoyPovUp && key <= IK_JoyPovRight);
}

void GamepadInput::AddDefaultBindings(Engine* engine)
{
	PackageManager* packages = engine->packages.get();
	if (!engine->LaunchInfo.IsDeusEx() || packages->GetIniValue("user", SettingsSection, "BindingsVersion") == BindingsVersion)
		return;

	// The joystick bindings of the original game are for a joystick of the year 2000, and this engine had no
	// controller before. So these bindings replace them.
	std::string section = engine->InputIniSection();
	for (const auto& binding : DefaultBindings)
		packages->SetIniValue("user", section, binding.first, binding.second);

	if (!packages->HasIniKey("user", SettingsSection, "DeadZone"))
		packages->SetIniValue("user", SettingsSection, "DeadZone", "0.15");
	if (!packages->HasIniKey("user", SettingsSection, "LookCurve"))
		packages->SetIniValue("user", SettingsSection, "LookCurve", "2.0");
	if (!packages->HasIniKey("user", SettingsSection, "CursorSpeed"))
		packages->SetIniValue("user", SettingsSection, "CursorSpeed", "480");
	packages->SetIniValue("user", SettingsSection, "BindingsVersion", BindingsVersion);
	LogMessage("Gamepad: added the default controller bindings to the user ini");
}

void GamepadInput::LoadSettings(Engine* engine)
{
	settingsLoaded = true;
	auto read = [&](const char* key, float value) {
		std::string text = engine->packages->GetIniValue("user", SettingsSection, key);
		return text.empty() ? value : (float)std::atof(text.c_str());
	};
	deadZone = std::clamp(read("DeadZone", deadZone), 0.0f, 0.9f);
	lookCurve = std::clamp(read("LookCurve", lookCurve), 0.5f, 4.0f);
	cursorSpeed = std::max(read("CursorSpeed", cursorSpeed), 1.0f);
}

void GamepadInput::Tick(Engine* engine, float elapsed)
{
	if (!settingsLoaded)
		LoadSettings(engine);

	// A test script drives a controller of its own, and the real controller has no effect on the test
	GamepadState state;
	bool fake = Harness::Get().GetFakeGamepad(state);
	if (!fake)
	{
		if (!device)
			device = GamepadDevice::Create();
		device->Read(state);
	}
	trace = (fake && state.Connected) || Harness::Get().TraceKeys;

	if (state.Connected != connected || (state.Connected && state.Name != name))
	{
		LogMessage(state.Connected ? "Gamepad: connected " + state.Name : "Gamepad: disconnected " + name);
		connected = state.Connected;
		name = state.Name;
	}

	// The triggers are buttons that go down at more than 35% and up at less than 20%
	leftTriggerDown = state.LeftTrigger > (leftTriggerDown ? 0.2f : 0.35f);
	rightTriggerDown = state.RightTrigger > (rightTriggerDown ? 0.2f : 0.35f);
	state.Buttons[(int)GamepadButton::LeftTrigger] = leftTriggerDown;
	state.Buttons[(int)GamepadButton::RightTrigger] = rightTriggerDown;

	Mode newMode = GetMode(engine);
	if (newMode != mode)
		ChangeMode(engine, newMode);

	// The releases come first, so a button that goes up and another that goes down in the same frame keep their order
	for (int i = 0; i < GamepadButtonCount; i++)
	{
		if (!state.Buttons[i] && held[i].Down)
			Release(engine, (GamepadButton)i);
	}
	for (int i = 0; i < GamepadButtonCount; i++)
	{
		if (state.Buttons[i] && !held[i].Down)
			Press(engine, (GamepadButton)i);
	}

	// A press can open or close a screen
	newMode = GetMode(engine);
	if (newMode != mode)
		ChangeMode(engine, newMode);

	if (mode == Mode::Game)
	{
		SendSticks(engine, state);
	}
	else if (mode == Mode::Menu)
	{
		UpdateDirection(engine, state, elapsed);
		MoveCursor(engine, state, elapsed);
	}
}

GamepadInput::Mode GamepadInput::GetMode(Engine* engine)
{
	URootWindow* root = engine->dxRootWindow;
	if (!root || !root->IsModalOpen())
		return Mode::Game;

	for (UWindow* window = root->GetKeyboardTarget(); window; window = window->parentOwner())
	{
		if (window->IsA("MenuScreenCustomizeKeys"))
			return window->GetBool("bWaitingForInput") ? Mode::KeyCapture : Mode::Menu;
	}
	return Mode::Menu;
}

void GamepadInput::ChangeMode(Engine* engine, Mode newMode)
{
	if (mode == Mode::Game)
		StopSticks(engine);
	else if (mode == Mode::Menu)
		StopDirection(engine);

	mode = newMode;
	stickCentered = false;
	Trace(std::string("Gamepad: mode ") + ModeName[(int)mode]);
}

void GamepadInput::Press(Engine* engine, GamepadButton button)
{
	HeldButton& h = held[(int)button];
	h.Down = true;
	h.Kind = SentKind::None;
	h.Key = IK_None;

	// While the key screen waits for a key, Start is Escape, so that the player can stop the wait with the controller
	if (mode == Mode::Game || (mode == Mode::KeyCapture && button != GamepadButton::Start))
	{
		EInputKey key = GameKey(button);
		const std::string* binding = nullptr;
		if (mode == Mode::Game && button != GamepadButton::LeftShoulder && held[(int)GamepadButton::LeftShoulder].Down)
		{
			auto it = engine->keybindings.find(std::string("Layer") + Engine::keynames[key]);
			if (it != engine->keybindings.end() && !it->second.empty())
				binding = &it->second;
		}

		h.Kind = mode == Mode::Game ? SentKind::GameKey : SentKind::WindowKey;
		h.Key = key;
		Trace(std::string("Gamepad: press ") + Engine::keynames[key] + (binding ? " (layer: " + *binding + ")" : ""));
		SendKey(engine, key, true, binding);
		return;
	}

	// A screen is open
	EInputKey key = IK_None;
	switch (button)
	{
	case GamepadButton::A: key = IK_Enter; break;
	case GamepadButton::B:
	case GamepadButton::Back:
	case GamepadButton::Start: key = IK_Escape; break;
	case GamepadButton::X: key = IK_Backspace; break;
	case GamepadButton::Y: key = IK_Delete; break;
	case GamepadButton::LeftTrigger: h.Kind = SentKind::MouseButton; h.Key = IK_RightMouse; break;
	case GamepadButton::RightTrigger: h.Kind = SentKind::MouseButton; h.Key = IK_LeftMouse; break;
	case GamepadButton::LeftShoulder:
	case GamepadButton::RightShoulder:
		if (engine->dxRootWindow)
		{
			EInputKey wheel = button == GamepadButton::LeftShoulder ? IK_MouseWheelUp : IK_MouseWheelDown;
			Trace(std::string("Gamepad: menu ") + Engine::keynames[wheel]);
			if (engine->dxRootWindow->OnWindowMouseWheel(Point(0.0, 0.0), wheel))
				engine->ReleaseGameInput();
		}
		return;
	default:
		if (IsDPad(button))
			h.Kind = SentKind::Direction;
		return;
	}

	if (h.Kind == SentKind::MouseButton)
	{
		Trace(std::string("Gamepad: menu ") + Engine::keynames[h.Key] + " down");
		SendMouseButton(engine, h.Key, true);
	}
	else if (key != IK_None)
	{
		h.Kind = SentKind::WindowKey;
		h.Key = key;
		Trace(std::string("Gamepad: menu ") + Engine::keynames[key]);
		SendKey(engine, key, true);
	}
}

void GamepadInput::Release(Engine* engine, GamepadButton button)
{
	HeldButton h = held[(int)button];
	held[(int)button] = {};

	if (h.Kind == SentKind::GameKey || h.Kind == SentKind::WindowKey)
	{
		Trace(std::string("Gamepad: release ") + Engine::keynames[h.Key]);
		SendKey(engine, h.Key, false);
	}
	else if (h.Kind == SentKind::MouseButton)
	{
		Trace(std::string("Gamepad: menu ") + Engine::keynames[h.Key] + " up");
		SendMouseButton(engine, h.Key, false);
	}
}

void GamepadInput::SendSticks(Engine* engine, const GamepadState& state)
{
	static const EInputKey keys[4] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV };
	float values[4];
	ApplyDeadZone(state.LeftX, state.LeftY, deadZone, 1.0f, values[0], values[1]);
	ApplyDeadZone(state.RightX, state.RightY, deadZone, lookCurve, values[2], values[3]);

	// As UWindowsViewport::UpdateInput in WinDrv.dll, a stick out of the middle gives an axis event in each frame
	for (int i = 0; i < 4; i++)
	{
		if (values[i] != 0.0f || sentAxes[i] != 0.0f)
		{
			engine->InputEvent(keys[i], IST_Axis, values[i]);
			sentAxes[i] = values[i];
		}
	}
}

void GamepadInput::StopSticks(Engine* engine)
{
	static const EInputKey keys[4] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV };
	for (int i = 0; i < 4; i++)
	{
		if (sentAxes[i] != 0.0f)
		{
			engine->InputEvent(keys[i], IST_Axis, 0.0f);
			sentAxes[i] = 0.0f;
		}
	}
}

void GamepadInput::UpdateDirection(Engine* engine, const GamepadState& state, float elapsed)
{
	// The D-pad gives an arrow key only for a press while the screen is open
	auto pressedHere = [&](GamepadButton button) { return held[(int)button].Down && held[(int)button].Kind == SentKind::Direction; };

	EInputKey key = IK_None;
	if (pressedHere(GamepadButton::DPadUp))
		key = IK_Up;
	else if (pressedHere(GamepadButton::DPadDown))
		key = IK_Down;
	else if (pressedHere(GamepadButton::DPadLeft))
		key = IK_Left;
	else if (pressedHere(GamepadButton::DPadRight))
		key = IK_Right;

	// The left stick gives an arrow key at more than half of its way, and it keeps it to 35%. A stick that was out of
	// the middle when the screen opened, for example while the player walked, must first come back.
	float length = std::sqrt(state.LeftX * state.LeftX + state.LeftY * state.LeftY);
	if (length < 0.35f)
		stickCentered = true;
	bool stickArrow = directionKey != IK_None && key == IK_None ? length >= 0.35f : length > 0.5f;
	if (key == IK_None && stickCentered && stickArrow)
	{
		if (std::abs(state.LeftX) > std::abs(state.LeftY))
			key = state.LeftX > 0.0f ? IK_Right : IK_Left;
		else
			key = state.LeftY > 0.0f ? IK_Up : IK_Down;
	}

	if (key != directionKey)
	{
		StopDirection(engine);
		if (key != IK_None)
		{
			Trace(std::string("Gamepad: menu ") + Engine::keynames[key]);
			directionKey = key;
			directionRepeat = 0.4f;
			SendKey(engine, key, true);
		}
	}
	else if (key != IK_None)
	{
		// The key repeats as a held key of a keyboard does: a second key down with no key up
		directionRepeat -= elapsed;
		if (directionRepeat <= 0.0f)
		{
			directionRepeat = 0.1f;
			SendKey(engine, key, true);
		}
	}
}

void GamepadInput::StopDirection(Engine* engine)
{
	if (directionKey != IK_None)
	{
		SendKey(engine, directionKey, false);
		directionKey = IK_None;
	}
}

void GamepadInput::MoveCursor(Engine* engine, const GamepadState& state, float elapsed)
{
	if (!engine->dxRootWindow)
		return;

	float x = 0.0f, y = 0.0f;
	ApplyDeadZone(state.RightX, state.RightY, deadZone, 2.0f, x, y);
	if (x != 0.0f || y != 0.0f)
		engine->dxRootWindow->MoveCursor(x * cursorSpeed * elapsed, -y * cursorSpeed * elapsed);
}

// The same path as a key of the keyboard (Engine::OnWindowKeyDown and OnWindowKeyUp): the window system of Deus Ex
// gets the key first while a screen is open, and the game input gets it when no screen is open
void GamepadInput::SendKey(Engine* engine, EInputKey key, bool down, const std::string* binding)
{
	URootWindow* root = engine->dxRootWindow;
	if (root && (down ? root->OnWindowKeyDown(key) : root->OnWindowKeyUp(key)))
	{
		engine->ReleaseGameInput();
		return;
	}
	engine->InputEvent(key, down ? IST_Press : IST_Release, 0.0f, binding);
}

// A mouse button at the cursor of the Deus Ex interface (Engine::OnWindowMouseDown and OnWindowMouseUp)
void GamepadInput::SendMouseButton(Engine* engine, EInputKey key, bool down)
{
	URootWindow* root = engine->dxRootWindow;
	if (root && (down ? root->OnWindowMouseDown(Point(0.0, 0.0), key) : root->OnWindowMouseUp(Point(0.0, 0.0), key)))
		engine->ReleaseGameInput();
}

void GamepadInput::Trace(const std::string& text)
{
	if (trace)
		LogMessage(text);
}
