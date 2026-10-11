
#include "Precomp.h"
#include "UButtonWindow.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"

// The functions of this file follow XButtonWindow in Extension.dll.

void UButtonWindow::InitWindow()
{
	bButtonPressed() = false;
	bMousePressed() = false;
	bAutoRepeat() = false;
	activateDelay() = 0.3f;
	initialDelay() = 0.5f;
	repeatRate() = 0.1f;
	lastInputKey() = 0;
	pressSound() = nullptr;
	clickSound() = nullptr;
	repeatTime() = 0.0f;
	curTexture() = nullptr;
	curTileColor() = { 255, 255, 255, 255 };
	curTextColor() = { 255, 255, 255, 255 };
	SetSelectability(true);

	UTextWindow::InitWindow();
}

void UButtonWindow::DrawWindow(UGC* gc)
{
	DrawRawBackground(gc);

	gc->SetStyle((EDrawStyle)backgroundStyle());
	gc->SetTileColor(curTileColor());
	gc->SetTextColor(curTextColor());
	gc->DrawPattern(0.0f, 0.0f, Width(), Height(), 0.0f, 0.0f, curTexture());

	// As XTextWindow::Draw: the script draws first, then the text goes on top
	gc->SetAlignments(HAlign(), VAlign());
	DrawWindowEvent(gc);

	if (normalFont())
	{
		float xMargin = hMargin();
		float yMargin = vMargin();
		float w = Width() - 2.0f * xMargin;
		float h = Height() - 2.0f * yMargin;
		if (w > 0.0f && h > 0.0f)
			gc->DrawText(xMargin, yMargin, w, h, Text());
	}
}

void UButtonWindow::ChangeButtonAppearance()
{
	// 0 = normal, 1 = the button has the keyboard focus, 2 = the button or a window above it is not sensitive
	int state = 2;
	UWindow* cur = this;
	while (cur && cur->bIsSensitive())
		cur = cur->parentOwner();
	if (!cur)
		state = IsFocusWindow() ? 1 : 0;

	int index = state * 2;
	if (bButtonPressed())
		index++;

	UTexture* texture = Info[index].tx;
	if (!texture && bButtonPressed())
		texture = Info[1].tx;
	if (!texture)
		texture = Info[0].tx;

	curTexture() = texture;
	curTileColor() = Info[index].tlColor;
	curTextColor() = Info[index].txColor;
}

void UButtonWindow::ActivateButton(EInputKey key)
{
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		bool handled = (key == IK_RightMouse) ? cur->ButtonActivatedRight(this) : cur->ButtonActivated(this);
		if (handled)
			break;
	}
}

void UButtonWindow::EnableAutoRepeat(std::optional<bool> bEnable, std::optional<float> newInitialDelay, std::optional<float> newRepeatRate)
{
	bAutoRepeat() = !bEnable || *bEnable;
	initialDelay() = newInitialDelay ? *newInitialDelay : 0.5f;
	repeatRate() = newRepeatRate ? *newRepeatRate : 0.1f;
}

void UButtonWindow::EnableRightMouseClick(std::optional<bool> bEnable)
{
	bEnableRightMouseClick() = !bEnable || *bEnable;
}

void UButtonWindow::PressButton(std::optional<uint8_t> Key)
{
	EInputKey key = Key ? (EInputKey)(*Key) : IK_LeftMouse;

	// The button looks pressed for a short time
	bButtonPressed() = true;
	ChangeButtonAppearance();
	activateTimeLeft = activateDelay();

	PlaySound(clickSound(), {}, {}, {}, {});
	lastInputKey() = (uint8_t)key;
	ActivateButton(key);
}

void UButtonWindow::SetActivateDelay(std::optional<float> newDelay)
{
	if (newDelay)
		activateDelay() = *newDelay;
}

void UButtonWindow::SetButtonColors(std::optional<Color> Normal, std::optional<Color> pressed, std::optional<Color> normalFocus, std::optional<Color> pressedFocus, std::optional<Color> normalInsensitive, std::optional<Color> pressedInsensitive)
{
	// A color that the script leaves out is white
	const Color white = { 255, 255, 255, 255 };
	Info[0].tlColor = Normal ? *Normal : white;
	Info[1].tlColor = pressed ? *pressed : white;
	Info[2].tlColor = normalFocus ? *normalFocus : white;
	Info[3].tlColor = pressedFocus ? *pressedFocus : white;
	Info[4].tlColor = normalInsensitive ? *normalInsensitive : white;
	Info[5].tlColor = pressedInsensitive ? *pressedInsensitive : white;
	ChangeButtonAppearance();
}

void UButtonWindow::SetButtonSounds(std::optional<UObject*> newPressSound, std::optional<UObject*> newClickSound)
{
	pressSound() = newPressSound ? UObject::Cast<USound>(*newPressSound) : nullptr;
	clickSound() = newClickSound ? UObject::Cast<USound>(*newClickSound) : nullptr;
}

void UButtonWindow::SetButtonTextures(std::optional<UObject*> Normal, std::optional<UObject*> pressed, std::optional<UObject*> normalFocus, std::optional<UObject*> pressedFocus, std::optional<UObject*> normalInsensitive, std::optional<UObject*> pressedInsensitive)
{
	Info[0].tx = Normal ? UObject::Cast<UTexture>(*Normal) : nullptr;
	Info[1].tx = pressed ? UObject::Cast<UTexture>(*pressed) : nullptr;
	Info[2].tx = normalFocus ? UObject::Cast<UTexture>(*normalFocus) : nullptr;
	Info[3].tx = pressedFocus ? UObject::Cast<UTexture>(*pressedFocus) : nullptr;
	Info[4].tx = normalInsensitive ? UObject::Cast<UTexture>(*normalInsensitive) : nullptr;
	Info[5].tx = pressedInsensitive ? UObject::Cast<UTexture>(*pressedInsensitive) : nullptr;
	ChangeButtonAppearance();
}

void UButtonWindow::SetTextColors(std::optional<Color> Normal, std::optional<Color> pressed, std::optional<Color> normalFocus, std::optional<Color> pressedFocus, std::optional<Color> normalInsensitive, std::optional<Color> pressedInsensitive)
{
	// A color that the script leaves out is green
	const Color green = { 0, 255, 0, 255 };
	Info[0].txColor = Normal ? *Normal : green;
	Info[1].txColor = pressed ? *pressed : green;
	Info[2].txColor = normalFocus ? *normalFocus : green;
	Info[3].txColor = pressedFocus ? *pressedFocus : green;
	Info[4].txColor = normalInsensitive ? *normalInsensitive : green;
	Info[5].txColor = pressedInsensitive ? *pressedInsensitive : green;
	ChangeButtonAppearance();
}

void UButtonWindow::SensitivityChanged(bool bNewSensitivity)
{
	UTextWindow::SensitivityChanged(bNewSensitivity);

	if (!bNewSensitivity && bMousePressed())
	{
		bButtonPressed() = false;
		bMousePressed() = false;
		UngrabMouse();
	}
	ChangeButtonAppearance();
}

void UButtonWindow::FocusEnteredWindow()
{
	UTextWindow::FocusEnteredWindow();
	ChangeButtonAppearance();
}

void UButtonWindow::FocusLeftWindow()
{
	UTextWindow::FocusLeftWindow();
	ChangeButtonAppearance();
}

void UButtonWindow::MouseMoved(float newX, float newY)
{
	UTextWindow::MouseMoved(newX, newY);

	if (bMousePressed())
	{
		bButtonPressed() = bIsVisible() && IsPointInWindow(newX, newY);
		ChangeButtonAppearance();
	}
}

bool UButtonWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UTextWindow::MouseButtonPressed(pointX, pointY, button, numClicks);

	if (button == IK_LeftMouse || (button == IK_RightMouse && bEnableRightMouseClick()))
	{
		bButtonPressed() = true;
		bMousePressed() = true;
		GrabMouse();
		ChangeButtonAppearance();

		if (!bAutoRepeat())
		{
			PlaySound(pressSound(), {}, {}, {}, {});
		}
		else
		{
			// A button with auto repeat does its work at the press and then again while the player holds it
			repeatTime() = initialDelay();
			PlaySound(clickSound(), {}, {}, {}, {});
			lastInputKey() = (uint8_t)button;
			ActivateButton(button);
		}
		handled = true;
	}

	return handled;
}

bool UButtonWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UTextWindow::MouseButtonReleased(pointX, pointY, button, numClicks);

	if (button == IK_LeftMouse || (button == IK_RightMouse && bEnableRightMouseClick()))
	{
		if (bMousePressed())
			UngrabMouse();

		repeatTime() = 0.0f;
		bButtonPressed() = false;
		bMousePressed() = false;
		lastInputKey() = 0;
		ChangeButtonAppearance();

		if (IsPointInWindow(pointX, pointY) && !bAutoRepeat())
		{
			PlaySound(clickSound(), {}, {}, {}, {});
			ActivateButton(button);
		}
		handled = true;
	}

	return handled;
}

void UButtonWindow::Tick(float timeElapsed)
{
	// The end of the pressed look that PressButton started
	if (activateTimeLeft > 0.0f)
	{
		activateTimeLeft -= timeElapsed;
		if (activateTimeLeft <= 0.0f)
		{
			activateTimeLeft = 0.0f;
			if (!bMousePressed())
			{
				bButtonPressed() = false;
				ChangeButtonAppearance();
			}
		}
	}

	if (bMousePressed() && bAutoRepeat())
	{
		repeatTime() -= timeElapsed;
		if (repeatTime() < 0.0f)
		{
			repeatTime() += repeatRate();
			if (repeatTime() < 0.0f)
				repeatTime() = repeatRate();

			float mouseX = 0.0f, mouseY = 0.0f;
			GetCursorPos(mouseX, mouseY);
			if (IsPointInWindow(mouseX, mouseY))
			{
				PlaySound(clickSound(), {}, {}, {}, {});
				ActivateButton((EInputKey)lastInputKey());
			}
		}
	}

	UTextWindow::Tick(timeElapsed);
}
