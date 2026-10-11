
#include "Precomp.h"
#include "UToggleWindow.h"
#include "Packages/Engine/Resources/USound.h"

// The functions of this file follow XToggleWindow in Extension.dll. The pressed flag of the button is the toggle.

void UToggleWindow::InitWindow()
{
	enableSound() = nullptr;
	disableSound() = nullptr;
	UButtonWindow::InitWindow();
}

void UToggleWindow::ChangeToggle()
{
	bool toggle = bButtonPressed();
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ToggleChanged(this, toggle))
			break;
	}
}

bool UToggleWindow::GetToggle()
{
	return bButtonPressed();
}

void UToggleWindow::SetToggle(bool bNewToggle)
{
	if (bButtonPressed() != bNewToggle)
	{
		bButtonPressed() = bNewToggle;
		ChangeButtonAppearance();
		ChangeToggle();
	}
}

void UToggleWindow::SetToggleSounds(std::optional<UObject*> newEnableSound, std::optional<UObject*> newDisableSound)
{
	enableSound() = newEnableSound ? UObject::Cast<USound>(*newEnableSound) : nullptr;
	disableSound() = newDisableSound ? UObject::Cast<USound>(*newDisableSound) : nullptr;
}

void UToggleWindow::PressButton(std::optional<uint8_t> Key)
{
	SetToggle(!GetToggle());
	PlaySound(GetToggle() ? enableSound() : disableSound(), {}, {}, {}, {});
}

bool UToggleWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UTextWindow::MouseButtonPressed(pointX, pointY, button, numClicks);
	if (button == IK_LeftMouse)
	{
		// The release of the button comes to this window also when the mouse is not on it then
		toggleMousePressed = true;
		GrabMouse();
		handled = true;
	}
	return handled;
}

bool UToggleWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UTextWindow::MouseButtonReleased(pointX, pointY, button, numClicks);
	if (button == IK_LeftMouse)
	{
		if (toggleMousePressed)
		{
			toggleMousePressed = false;
			UngrabMouse();
		}

		if (IsPointInWindow(pointX, pointY))
		{
			// The original selects the sound before it changes the toggle
			PlaySound(GetToggle() ? enableSound() : disableSound(), {}, {}, {}, {});
			SetToggle(!GetToggle());
		}
		handled = true;
	}
	return handled;
}
