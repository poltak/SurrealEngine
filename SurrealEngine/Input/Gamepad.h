#pragma once

#include <memory>
#include <string>

// The buttons of a controller with the layout of an Xbox controller. A PlayStation or a Switch Pro controller has
// buttons at the same places: A is the bottom face button, B the right one, X the left one and Y the top one.
// Back is the left button in the middle (View, Share, Create or Minus), Start the right one (Menu, Options or Plus).
enum class GamepadButton
{
	A, B, X, Y,
	LeftShoulder, RightShoulder,
	Back, Start,
	LeftThumb, RightThumb,
	LeftTrigger, RightTrigger,
	DPadUp, DPadDown, DPadLeft, DPadRight,
	Count
};

constexpr int GamepadButtonCount = (int)GamepadButton::Count;

struct GamepadState
{
	bool Connected = false;
	std::string Name;

	// The triggers are analog. GamepadInput makes buttons of them.
	bool Buttons[GamepadButtonCount] = {};

	// -1 to 1. Right and up are positive.
	float LeftX = 0.0f;
	float LeftY = 0.0f;
	float RightX = 0.0f;
	float RightY = 0.0f;

	// 0 to 1
	float LeftTrigger = 0.0f;
	float RightTrigger = 0.0f;
};

// Reads the controller of the computer. On macOS this is the Game Controller framework, which gives the input only
// to the app in front. The other platforms have no controller yet.
class GamepadDevice
{
public:
	static std::unique_ptr<GamepadDevice> Create();
	virtual ~GamepadDevice() = default;

	// The controller that the player used last, or no controller (Connected is false)
	virtual void Read(GamepadState& state) = 0;
};
