#import <GameController/GameController.h>

#include "Gamepad.h"

namespace
{
	class GamepadDeviceMac : public GamepadDevice
	{
	public:
		void Read(GamepadState& state) override
		{
			@autoreleasepool
			{
				state = {};

				// GCController.current is the controller that the player used last
				GCController* controller = nil;
				if (@available(macOS 11.0, *))
					controller = GCController.current;
				if (!controller.extendedGamepad)
				{
					controller = nil;
					for (GCController* candidate in GCController.controllers)
					{
						if (candidate.extendedGamepad)
						{
							controller = candidate;
							break;
						}
					}
				}

				GCExtendedGamepad* pad = controller.extendedGamepad;
				if (!pad)
					return;

				state.Connected = true;
				NSString* name = controller.vendorName;
				state.Name = name ? name.UTF8String : "Controller";

				auto set = [&](GamepadButton button, GCControllerButtonInput* input) { state.Buttons[(int)button] = input && input.pressed; };
				set(GamepadButton::A, pad.buttonA);
				set(GamepadButton::B, pad.buttonB);
				set(GamepadButton::X, pad.buttonX);
				set(GamepadButton::Y, pad.buttonY);
				set(GamepadButton::LeftShoulder, pad.leftShoulder);
				set(GamepadButton::RightShoulder, pad.rightShoulder);
				set(GamepadButton::Back, pad.buttonOptions);
				set(GamepadButton::Start, pad.buttonMenu);
				set(GamepadButton::LeftThumb, pad.leftThumbstickButton);
				set(GamepadButton::RightThumb, pad.rightThumbstickButton);
				set(GamepadButton::DPadUp, pad.dpad.up);
				set(GamepadButton::DPadDown, pad.dpad.down);
				set(GamepadButton::DPadLeft, pad.dpad.left);
				set(GamepadButton::DPadRight, pad.dpad.right);

				state.LeftX = pad.leftThumbstick.xAxis.value;
				state.LeftY = pad.leftThumbstick.yAxis.value;
				state.RightX = pad.rightThumbstick.xAxis.value;
				state.RightY = pad.rightThumbstick.yAxis.value;
				state.LeftTrigger = pad.leftTrigger.value;
				state.RightTrigger = pad.rightTrigger.value;
			}
		}
	};
}

std::unique_ptr<GamepadDevice> GamepadDevice::Create()
{
	return std::make_unique<GamepadDeviceMac>();
}
