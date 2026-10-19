#include "Precomp.h"
#include "Gamepad.h"

#ifndef __APPLE__

namespace
{
	class GamepadDeviceStub : public GamepadDevice
	{
	public:
		void Read(GamepadState& state) override
		{
			state = {};
		}
	};
}

std::unique_ptr<GamepadDevice> GamepadDevice::Create()
{
	return std::make_unique<GamepadDeviceStub>();
}

#endif
