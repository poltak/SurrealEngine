#pragma once

#include "UTextWindow.h"

class UButtonWindow : public UTextWindow
{
public:
	using UTextWindow::UTextWindow;

	void ActivateButton(EInputKey key);
	void EnableAutoRepeat(std::optional<bool> bEnable, std::optional<float> initialDelay, std::optional<float> repeatRate);
	void EnableRightMouseClick(std::optional<bool> bEnable);
	virtual void PressButton(std::optional<uint8_t> Key);
	void SetActivateDelay(std::optional<float> newDelay);
	void SetButtonColors(std::optional<Color> Normal, std::optional<Color> pressed, std::optional<Color> normalFocus, std::optional<Color> pressedFocus, std::optional<Color> normalInsensitive, std::optional<Color> pressedInsensitive);
	void SetButtonSounds(std::optional<UObject*> newPressSound, std::optional<UObject*> newClickSound);
	void SetButtonTextures(std::optional<UObject*> Normal, std::optional<UObject*> pressed, std::optional<UObject*> normalFocus, std::optional<UObject*> pressedFocus, std::optional<UObject*> normalInsensitive, std::optional<UObject*> pressedInsensitive);
	void SetTextColors(std::optional<Color> Normal, std::optional<Color> pressed, std::optional<Color> normalFocus, std::optional<Color> pressedFocus, std::optional<Color> normalInsensitive, std::optional<Color> pressedInsensitive);

	// Selects the texture and the colors for the state of the button
	void ChangeButtonAppearance();

	void InitWindow() override;
	void SensitivityChanged(bool bNewSensitivity) override;
	void FocusEnteredWindow() override;
	void FocusLeftWindow() override;
	void MouseMoved(float newX, float newY) override;
	bool MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks) override;
	bool MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks) override;
	void Tick(float timeElapsed) override;
	void DrawWindow(UGC* gc) override;

	//ButtonDisplayInfo& Info() { return Value<ButtonDisplayInfo>(PropOffsets_ButtonWindow.Info); }
	float& activateDelay() { return Value<float>(PropOffsets_ButtonWindow.activateDelay); }
	int& activateTimer() { return Value<int>(PropOffsets_ButtonWindow.activateTimer); }
	BitfieldBool bAutoRepeat() { return BoolValue(PropOffsets_ButtonWindow.bAutoRepeat); }
	BitfieldBool bButtonPressed() { return BoolValue(PropOffsets_ButtonWindow.bButtonPressed); }
	BitfieldBool bEnableRightMouseClick() { return BoolValue(PropOffsets_ButtonWindow.bEnableRightMouseClick); }
	BitfieldBool bMousePressed() { return BoolValue(PropOffsets_ButtonWindow.bMousePressed); }
	USound*& clickSound() { return Value<USound*>(PropOffsets_ButtonWindow.clickSound); }
	Color& curTextColor() { return Value<Color>(PropOffsets_ButtonWindow.curTextColor); }
	UTexture*& curTexture() { return Value<UTexture*>(PropOffsets_ButtonWindow.curTexture); }
	Color& curTileColor() { return Value<Color>(PropOffsets_ButtonWindow.curTileColor); }
	float& initialDelay() { return Value<float>(PropOffsets_ButtonWindow.initialDelay); }
	uint8_t& lastInputKey() { return Value<uint8_t>(PropOffsets_ButtonWindow.lastInputKey); }
	USound*& pressSound() { return Value<USound*>(PropOffsets_ButtonWindow.pressSound); }
	float& repeatRate() { return Value<float>(PropOffsets_ButtonWindow.repeatRate); }
	float& repeatTime() { return Value<float>(PropOffsets_ButtonWindow.repeatTime); }

	// The look of the button for each state. The order is the order of the arguments of SetButtonTextures.
	struct ButtonDisplayInfo
	{
		UTexture* tx = nullptr;
		Color tlColor = { 255, 255, 255, 255 };
		Color txColor = { 0, 255, 0, 255 };
	};
	ButtonDisplayInfo Info[6];

private:
	// The pressed look of PressButton ends after this time
	float activateTimeLeft = 0.0f;
};
