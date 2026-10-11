#pragma once

#include "UButtonWindow.h"

class UToggleWindow : public UButtonWindow
{
public:
	using UButtonWindow::UButtonWindow;

	void ChangeToggle();
	bool GetToggle();
	void SetToggle(bool bNewToggle);
	void SetToggleSounds(std::optional<UObject*> enableSound, std::optional<UObject*> disableSound);

	void InitWindow() override;
	void PressButton(std::optional<uint8_t> Key) override;
	bool MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks) override;
	bool MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks) override;

	USound*& disableSound() { return Value<USound*>(PropOffsets_ToggleWindow.disableSound); }
	USound*& enableSound() { return Value<USound*>(PropOffsets_ToggleWindow.enableSound); }

private:
	bool toggleMousePressed = false;
};
