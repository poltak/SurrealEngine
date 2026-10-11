
#include "Precomp.h"
#include "UModalWindow.h"
#include "URootWindow.h"

bool UModalWindow::IsCurrentModal()
{
	URootWindow* root = GetRootWindow();
	if (root)
	{
		for (UWindow* child = root->lastChild(); child; child = child->prevSibling())
		{
			if (child->bIsVisible())
			{
				if (auto modal = UObject::TryCast<UModalWindow>(child))
					return modal == this;
			}
		}
	}
	return false;
}

void UModalWindow::SetMouseFocusMode(uint8_t newFocusMode)
{
	focusMode() = newFocusMode;
}

void UModalWindow::InitWindow()
{
	// As XModalWindow::Init: the window under the mouse gets the keyboard focus, unless the script selects a different mode
	focusMode() = MFOCUS_Enter;
	preferredFocus() = nullptr;
	UTabGroupWindow::InitWindow();
}

void UModalWindow::DescendantRemoved(UWindow* descendant)
{
	// As XModalWindow::DescendantRemoved
	UTabGroupWindow::DescendantRemoved(descendant);
	if (preferredFocus() == descendant)
		preferredFocus() = nullptr;
}
