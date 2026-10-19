#pragma once

#include "Input/Gamepad.h"
#include "GameWindow.h"
#include <memory>
#include <string>

class Engine;

// Changes the state of a controller into input events of the engine, one time in each frame.
//
// In the game, a button is a joystick key of Unreal Engine 1, and the key bindings of the user ini give it its command:
// A, B, X and Y are Joy1 to Joy4, LB is Joy5, RB Joy6, Back Joy7, Start Joy8, L3 Joy9, R3 Joy10, LT Joy11, RT Joy12,
// and the D-pad is JoyPovUp, JoyPovDown, JoyPovLeft and JoyPovRight. This is the button order of an Xbox 360
// controller for the joystick code of the original WinDrv.dll (joyGetPosEx), with the two triggers as more buttons.
// The left stick is the axes JoyX and JoyY, the right stick JoyU and JoyV, as in the joystick bindings of Deus Ex.
//
// While the player holds LB, a button uses the binding "Layer" + key name (for example LayerJoy1) when it has one.
// The key bindings of the original have no such second layer.
//
// While a Deus Ex screen is open (a modal window has the input), the controller works the interface: the D-pad and
// the left stick are the arrow keys, A is Enter, B, Back and Start are Escape, X is Backspace and Y is Delete. The
// right stick moves the mouse cursor, RT is the left mouse button, LT the right mouse button, and LB and RB turn
// the mouse wheel. While the Keyboard/Mouse Settings screen waits for a key, a button gives its joystick key, so
// that the player can bind it there.
class GamepadInput
{
public:
	GamepadInput();
	~GamepadInput();

	// Read the controller and send its input. The engine calls this one time in each frame, after the window events.
	void Tick(Engine* engine, float elapsed);

	// Deus Ex: put the controller bindings and settings of this engine into the user ini. This happens one time.
	// After that the bindings belong to the player.
	static void AddDefaultBindings(Engine* engine);

	// The keys that can have a binding for the second layer
	static bool IsLayerKey(EInputKey key);

private:
	enum class Mode { Game, Menu, KeyCapture };
	enum class SentKind { None, GameKey, WindowKey, MouseButton, Direction };

	struct HeldButton
	{
		bool Down = false;
		SentKind Kind = SentKind::None;
		EInputKey Key = IK_None;
	};

	void LoadSettings(Engine* engine);
	Mode GetMode(Engine* engine);
	void ChangeMode(Engine* engine, Mode newMode);
	void Press(Engine* engine, GamepadButton button);
	void Release(Engine* engine, GamepadButton button);
	void SendSticks(Engine* engine, const GamepadState& state);
	void StopSticks(Engine* engine);
	void UpdateDirection(Engine* engine, const GamepadState& state, float elapsed);
	void StopDirection(Engine* engine);
	void MoveCursor(Engine* engine, const GamepadState& state, float elapsed);

	void SendKey(Engine* engine, EInputKey key, bool down, const std::string* binding = nullptr);
	void SendMouseButton(Engine* engine, EInputKey key, bool down);
	void Trace(const std::string& text);

	std::unique_ptr<GamepadDevice> device;
	bool settingsLoaded = false;
	float deadZone = 0.15f;
	float lookCurve = 2.0f;
	float cursorSpeed = 480.0f;

	bool trace = false;
	bool connected = false;
	std::string name;
	Mode mode = Mode::Game;
	HeldButton held[GamepadButtonCount];
	bool leftTriggerDown = false;
	bool rightTriggerDown = false;
	float sentAxes[4] = {}; // JoyX, JoyY, JoyU, JoyV

	EInputKey directionKey = IK_None;
	float directionRepeat = 0.0f;
	bool stickCentered = false; // the left stick gives no arrow key in a menu before it was back in the middle
};
