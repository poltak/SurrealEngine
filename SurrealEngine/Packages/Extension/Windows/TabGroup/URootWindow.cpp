
#include "Precomp.h"
#include "URootWindow.h"
#include "Harness.h"
#include "Utils/Logger.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Engine.h"

void URootWindow::EnablePositionalSound(std::optional<bool> bEnable)
{
	bPositionalSound() = !bEnable || *bEnable;
}

void URootWindow::EnableRendering(std::optional<bool> newRender)
{
	bRender() = !newRender || *newRender;
}

UObject* URootWindow::GenerateSnapshot(std::optional<bool> bFilter)
{
	LogUnimplemented("RootWindow.GenerateSnapshot");
	return nullptr;
}

bool URootWindow::IsPositionalSoundEnabled()
{
	return bPositionalSound();
}

bool URootWindow::IsRenderingEnabled()
{
	return bRender();
}

void URootWindow::LockMouse(std::optional<bool> bLockMove, std::optional<bool> bLockButton)
{
	// As XRootWindow::LockMouse. An argument that the script leaves out is true.
	bMouseMoveLocked() = !bLockMove || *bLockMove;
	bMouseButtonLocked() = !bLockButton || *bLockButton;
}

void URootWindow::SetDefaultEditCursor(std::optional<UObject*> newEditCursor)
{
	defaultEditCursor() = newEditCursor ? UObject::Cast<UTexture>(*newEditCursor) : nullptr;
}

void URootWindow::SetDefaultMovementCursors(std::optional<UObject*> newMovementCursor, std::optional<UObject*> newHorizontalMovementCursor, std::optional<UObject*> newVerticalMovementCursor, std::optional<UObject*> newTopLeftMovementCursor, std::optional<UObject*> newTopRightMovementCursor)
{
	if (newMovementCursor)
		DefaultMoveCursor() = UObject::Cast<UTexture>(*newMovementCursor);
	if (newHorizontalMovementCursor)
		defaultHorizontalMoveCursor() = UObject::Cast<UTexture>(*newHorizontalMovementCursor);
	if (newVerticalMovementCursor)
		defaultVerticalMoveCursor() = UObject::Cast<UTexture>(*newVerticalMovementCursor);
	if (newTopLeftMovementCursor)
		defaultTopLeftMoveCursor() = UObject::Cast<UTexture>(*newTopLeftMovementCursor);
	if (newTopRightMovementCursor)
		defaultTopRightMoveCursor() = UObject::Cast<UTexture>(*newTopRightMovementCursor);
}

void URootWindow::SetRawBackground(std::optional<UObject*> NewTexture, std::optional<Color> NewColor)
{
	if (NewTexture)
		rawBackground() = UObject::Cast<UTexture>(*NewTexture);
	if (NewColor)
		rawColor() = *NewColor;
}

void URootWindow::SetRawBackgroundSize(float newWidth, float NewHeight)
{
	rawBackgroundWidth() = newWidth;
	rawBackgroundHeight() = NewHeight;
}

void URootWindow::SetRenderViewport(float newX, float newY, float newWidth, float NewHeight)
{
	renderX() = newX;
	renderY() = newY;
	renderWidth() = newWidth;
	renderHeight() = NewHeight;
	RenderViewportSet = true;
}

void URootWindow::ResetRenderViewport()
{
	RenderViewportSet = false;
}

void URootWindow::SetSnapshotSize(float newWidth, float NewHeight)
{
	LogUnimplemented("RootWindow.SetSnapshotSize");
}

void URootWindow::ShowCursor(std::optional<bool> bShow)
{
	bCursorVisible() = !bShow || *bShow;
}

void URootWindow::StretchRawBackground(std::optional<bool> bStretch)
{
	bStretchRawBackground() = !bStretch || *bStretch;
}

void URootWindow::InitWindow()
{
	// As XRootWindow::Init: the cursor shows and the mouse is free
	bCursorVisible() = true;
	bMouseMoved() = true;
	bMouseMoveLocked() = false;
	bMouseButtonLocked() = false;
	UModalWindow::InitWindow();
}

void URootWindow::DescendantRemoved(UWindow* descendant)
{
	// As XRootWindow::DescendantRemoved: the click count does not go on with a window that is gone
	if (lastButtonWindow() == descendant)
		lastButtonWindow() = nullptr;
	UModalWindow::DescendantRemoved(descendant);
}

void URootWindow::WindowReady()
{
	SetRootCursorPos(GetVirtualWidth() * 0.5f, GetVirtualHeight() * 0.5f);
	UModalWindow::WindowReady();
}

bool URootWindow::IsCursorVisible()
{
	// As XRootWindow::PaintWindows: the cursor shows while the window system has the mouse, unless the script hid it
	// with ShowCursor. The script hides it while a person speaks in a conversation, in the credits, and while the
	// keyboard screen waits for a key.
	return IsModalOpen() && bCursorVisible();
}

void URootWindow::PostDrawWindow(UGC* gc)
{
	UModalWindow::PostDrawWindow(gc);
	if (IsCursorVisible())
	{
		// As XRootWindow::PaintWindows: the window under the mouse, or the window that has the mouse, selects the cursor.
		float relativeX = 0.0f, relativeY = 0.0f;
		UWindow* focus = GetCursorFocus(relativeX, relativeY);
		if (!focus)
			focus = this;

		float hotX = 0.0f, hotY = 0.0f;
		Color color = { 255, 255, 255, 255 };
		UTexture* shadow = nullptr;
		if (UTexture* cursor = focus->QueryCursor(relativeX, relativeY, hotX, hotY, color, shadow))
		{
			if (shadow)
			{
				gc->SetStyle(EDrawStyle::Modulated);
				gc->DrawIcon(MouseX() - hotX, MouseY() - hotY, shadow);
			}
			gc->SetStyle(EDrawStyle::Masked);
			gc->SetTileColor(color);
			gc->DrawIcon(MouseX() - hotX, MouseY() - hotY, cursor);
		}
	}
}

// As XWindow::SetFocusWindow in Extension.dll
bool URootWindow::SetRootFocusWindow(UWindow* newFocusWindow)
{
	// The focus goes to the window, or to the first window above it that is selectable
	for (UWindow* w = newFocusWindow; w; w = w->parentOwner())
	{
		if (w->bIsSelectable())
		{
			newFocusWindow = w;
			break;
		}
	}

	UWindow* oldFocusWindow = FocusWindow();
	if (oldFocusWindow == newFocusWindow)
		return true;

	if (newFocusWindow)
	{
		if (!newFocusWindow->IsTraversable(true))
			return false;

		// The modal window keeps this window. The focus comes back to it when a window on top of the modal window closes.
		if (UModalWindow* modal = UObject::Cast<UModalWindow>(newFocusWindow->GetModalWindow()))
			modal->preferredFocus() = newFocusWindow;
	}

	FocusWindow() = newFocusWindow;

	if (oldFocusWindow)
	{
		oldFocusWindow->FocusLeftWindow();
		if (oldFocusWindow->unfocusSound())
			oldFocusWindow->PlaySound(oldFocusWindow->unfocusSound(), {}, {}, {}, {});
		for (UWindow* w = oldFocusWindow; w; w = w->parentOwner())
			w->FocusLeftDescendant(oldFocusWindow);
	}

	// A script event can move the focus again, so use the window that has the focus now
	if (UWindow* focus = FocusWindow())
	{
		// A scroll area brings the window with the focus into view
		focus->AskParentToShowArea(0.0f, 0.0f, focus->Width(), focus->Height());
		if (focus->focusSound())
			focus->PlaySound(focus->focusSound(), {}, {}, {}, {});
		focus->FocusEnteredWindow();
		for (UWindow* w = focus; w; w = w->parentOwner())
			w->FocusEnteredDescendant(focus);
	}
	return true;
}

void URootWindow::SetRootCursorPos(float newMouseX, float newMouseY)
{
	// Clip cursor to the entire screen, not the root window box:

	newMouseX += UsedX;
	newMouseY += UsedY;

	float scale = GetVirtualScale();
	float realWidth = std::ceil(engine->viewport->ViewportWidth() / scale);
	float realHeight = std::ceil(engine->viewport->ViewportHeight() / scale);

	newMouseX = std::max(newMouseX, 0.0f);
	newMouseY = std::max(newMouseY, 0.0f);
	newMouseX = std::min(newMouseX, realWidth);
	newMouseY = std::min(newMouseY, realHeight);

	newMouseX -= UsedX;
	newMouseY -= UsedY;

	// Apply the new cursor pos:

	MouseX() = newMouseX;
	MouseY() = newMouseY;
	bMouseMoved() = true;
	ProcessMouseMove();
}

// As XRootWindow::ProcessMouseMove in Extension.dll
void URootWindow::ProcessMouseMove()
{
	if (!bMouseMoved())
		return;

	UModalWindow* modal = UObject::Cast<UModalWindow>(GetCurrentModal());
	uint8_t mode = modal ? modal->focusMode() : (uint8_t)MFOCUS_None;

	float relativeX = 0.0f, relativeY = 0.0f;
	UWindow* mouseWindow = GetCursorFocus(relativeX, relativeY);
	UWindow* lastWindow = UObject::Cast<UWindow>(lastMouseWindow());
	if (lastWindow != mouseWindow)
	{
		// The mouse focus mode of the modal window on top tells if the window under the mouse gets the keyboard focus
		if (lastWindow)
		{
			lastWindow->MouseLeftWindow();
			if (mode > MFOCUS_Enter)
				SetRootFocusWindow(nullptr);
		}
		if (mouseWindow)
		{
			mouseWindow->MouseEnteredWindow();
			if (mode > MFOCUS_Click)
				SetRootFocusWindow(mouseWindow);
		}
		lastMouseWindow() = mouseWindow;
	}

	if (prevMouseX() != MouseX() || prevMouseY() != MouseY())
	{
		if (mouseWindow)
		{
			ConvertCoordinates(this, MouseX(), MouseY(), mouseWindow, relativeX, relativeY);
			mouseWindow->MouseMoved(relativeX, relativeY);
		}
		prevMouseX() = MouseX();
		prevMouseY() = MouseY();
	}

	bMouseMoved() = false;
}

// As XRootWindow::GetMouseWindow in Extension.dll: the window that has the mouse grabbed, or the top window under the
// cursor in the modal window on top, or that modal window. A window of a screen below that modal window gets no mouse.
UWindow* URootWindow::GetCursorFocus(float& relativeX, float& relativeY)
{
	if (UWindow* grab = grabbedWindow())
	{
		ConvertCoordinates(this, MouseX(), MouseY(), grab, relativeX, relativeY);
		return grab;
	}

	UWindow* modal = GetCurrentModal();
	float x = 0.0f, y = 0.0f;
	ConvertCoordinates(this, MouseX(), MouseY(), modal, x, y);
	if (UWindow* found = modal->FindWindowByPoint(x, y, relativeX, relativeY))
		return found;

	relativeX = x;
	relativeY = y;
	return modal;
}

bool URootWindow::OnWindowMouseMove(const Point& pos)
{
#if 0 // We currently handle this in OnWindowRawMouseMove
	if (IsCursorVisible())
		SetRootCursorPos((float)pos.x / scale, (float)pos.y / scale);
#endif
	return IsModalOpen();
}

// Mouse routing follows XRootWindow in Extension.dll (build 1112fm):
//
// - XRootWindow::Process gives a mouse button to the window system only while a modal window is visible.
//   With no visible modal window, the game input gets it. While the mouse is grabbed, the window system
//   consumes every mouse button event, handled or not.
// - XRootWindow::GrabMouseEvents forgets the buttons that are down when the grab starts. So a button that
//   the player pressed in the game gives no release event to a window.
// - XRootWindow::HandleButtons drops the buttons and XRootWindow::HandleMouse drops the movement while the
//   script has them locked with LockMouse.

void URootWindow::UpdateInputGrab()
{
	bool grabbed = IsModalOpen();
	if (grabbed && !inputGrabbed)
	{
		for (bool& down : keyDownState)
			down = false;
	}
	inputGrabbed = grabbed;
}

bool URootWindow::OnWindowMouseDown(const Point& pos, EInputKey key)
{
	UpdateInputGrab();
	if ((int)key >= 0 && (int)key < 256)
		keyDownState[key] = true;

	if (!IsModalOpen())
		return false;
	if (bMouseButtonLocked())
		return true;

	// As XRootWindow::HandleButtons: the window under the mouse grabs the mouse until the button goes up, and it gets
	// the keyboard focus unless the modal window on top has no mouse focus mode
	float relativeX = 0.0f, relativeY = 0.0f;
	UWindow* mouseWindow = GetCursorFocus(relativeX, relativeY);
	mouseWindow->GrabMouse();
	UModalWindow* modal = UObject::Cast<UModalWindow>(GetCurrentModal());
	if (modal && modal->focusMode() != MFOCUS_None)
		SetRootFocusWindow(mouseWindow);

	// The event goes to the window and then to each window above it, until a window handles it
	int numClicks = UpdateClickCount(mouseWindow, key, true);
	for (UWindow* cur = mouseWindow; cur; )
	{
		UWindow* parent = cur->parentOwner();
		ConvertCoordinates(this, MouseX(), MouseY(), cur, relativeX, relativeY);
		if (cur->RawMouseButtonPressed(relativeX, relativeY, key, EInputType::IST_Press))
			break;
		if (cur->MouseButtonPressed(relativeX, relativeY, key, numClicks))
			break;
		cur = parent;
	}

	return true;
}

// XRootWindow::HandleButtons counts the clicks. A press on the same window with the same button, before the
// time limit ends and inside the distance limit, is one more click. A list uses the count for its double click.
int URootWindow::UpdateClickCount(UWindow* window, EInputKey key, bool press)
{
	if (multiClickTimeout() <= 0.0f)
		multiClickTimeout() = 0.5f;
	if (maxMouseDist() <= 0.0f)
		maxMouseDist() = 10.0f;

	float now = GetTickOffset();
	float timeLeft = lastButtonPress() - now;
	if (timeLeft <= 0.0f || window != lastButtonWindow() || (int)key != lastButtonType() ||
		std::abs(MouseX() - firstButtonMouseX()) > maxMouseDist() || std::abs(MouseY() - firstButtonMouseY()) > maxMouseDist())
	{
		timeLeft = 0.0f;
		lastButtonPress() = 0.0f;
		lastButtonType() = (int)key;
		lastButtonWindow() = window;
		clickCount() = 0;
	}

	if (press)
	{
		if (timeLeft > 0.0f)
			clickCount()++;
		lastButtonPress() = now + multiClickTimeout();
		if (clickCount() < 1)
		{
			firstButtonMouseX() = MouseX();
			firstButtonMouseY() = MouseY();
		}
	}

	return clickCount() + 1;
}

bool URootWindow::OnWindowMouseDoubleclick(const Point& pos, EInputKey key)
{
	// The window system sends a double click event in the place of the second mouse down event.
	// The root window counts the clicks itself, so this is a normal press.
	return OnWindowMouseDown(pos, key);
}

bool URootWindow::OnWindowMouseUp(const Point& pos, EInputKey key)
{
	UpdateInputGrab();
	bool wasDown = true;
	if ((int)key >= 0 && (int)key < 256)
	{
		wasDown = keyDownState[key];
		keyDownState[key] = false;
	}

	if (!IsModalOpen())
		return false;
	if (!wasDown || bMouseButtonLocked())
		return true;

	// As XRootWindow::HandleButtons: the window that grabbed the mouse at the press gets the release
	float relativeX = 0.0f, relativeY = 0.0f;
	UWindow* mouseWindow = GetCursorFocus(relativeX, relativeY);
	mouseWindow->UngrabMouse();

	int numClicks = UpdateClickCount(mouseWindow, key, false);
	for (UWindow* cur = mouseWindow; cur; )
	{
		UWindow* parent = cur->parentOwner();
		ConvertCoordinates(this, MouseX(), MouseY(), cur, relativeX, relativeY);
		if (cur->RawMouseButtonPressed(relativeX, relativeY, key, EInputType::IST_Release))
			break;
		if (cur->MouseButtonReleased(relativeX, relativeY, key, numClicks))
			break;
		cur = parent;
	}

	return true;
}

bool URootWindow::OnWindowMouseWheel(const Point& pos, EInputKey key)
{
	if (!OnWindowMouseDown(pos, key))
		return false;

	OnWindowMouseUp(pos, key);
	return true;
}

bool URootWindow::OnWindowRawMouseMove(int dx, int dy)
{
	// As XRootWindow::SetMouseDelta and HandleMouse: the cursor moves while the window system has the mouse, also
	// when the cursor does not show. It does not move while the script has the movement locked.
	if (!IsModalOpen())
		return false;

	if (!bMouseMoveLocked())
	{
		float mouseSpeed = 1.0f / GetVirtualScale();
		SetRootCursorPos(MouseX() + dx * mouseSpeed, MouseY() + dy * mouseSpeed);
	}
	return true;
}

// Keyboard routing follows XRootWindow in Extension.dll (build 1112fm):
//
// - XModalWindow::VisibilityChanged grabs the keyboard and the mouse for the window system while a
//   modal window is visible. With no visible modal window, key events go to the game input.
// - HandleVirtualKeyboard and HandleKeyboard start at the focus window. With no focus window they
//   start at the top visible modal window, or at the root window.
// - An event that a window does not handle goes to its parent, up to the root window.
// - While the keyboard is grabbed, the window system consumes every key event, handled or not.

UWindow* URootWindow::GetCurrentModal()
{
	for (UWindow* child = lastChild(); child; child = child->prevSibling())
	{
		// windowType is 2 (modal) or 3 (root) in the original. SurrealEngine does not fill that field, so test the class.
		if (child->bIsVisible() && UObject::TryCast<UModalWindow>(child))
			return child;
	}
	return this;
}

UWindow* URootWindow::GetKeyboardTarget()
{
	UWindow* target = FocusWindow();
	return target ? target : GetCurrentModal();
}

bool URootWindow::HandleVirtualKeyboard(EInputKey key, EInputType action, bool repeat)
{
	bool trace = Harness::Get().TraceKeys;
	bool handled = false;
	for (UWindow* target = GetKeyboardTarget(); target; target = target->parentOwner())
	{
		if (trace)
			LogMessage("Key trace: key " + std::to_string((int)key) + " action " + std::to_string((int)action) + " -> " + UObject::GetUClassFullName(target).ToString());
		if (!repeat)
			handled = target->RawKeyPressed(key, action, false);
		if (handled)
		{
			if (trace) LogMessage("Key trace: handled by RawKeyPressed");
			break;
		}
		if (action == EInputType::IST_Press)
		{
			handled = target->VirtualKeyPressed(key, repeat);
			if (handled)
			{
				if (trace) LogMessage("Key trace: handled by VirtualKeyPressed");
				break;
			}
		}
	}
	return handled;
}

bool URootWindow::OnWindowKeyChar(std::string chars)
{
	if (!IsModalOpen())
		return false;

	bool altDown = engine->window->GetKeyState(IK_Alt);
	for (UWindow* target = GetKeyboardTarget(); target; target = target->parentOwner())
	{
		// To do: this needs the accelerator table of the modal window (XModalWindow::KeyPressed)
		if (altDown && target->acceleratorKey() != 0 && target->AcceleratorKeyPressed(std::string(1, (char)target->acceleratorKey())))
			break;

		if ((UObject::TryCast<UModalWindow>(target) || !altDown) && target->KeyPressed(chars))
			break;
	}

	// To do: fire these for edit windows
	// event bool TextChanged(window edit, bool bModified)
	// event bool EditActivated(window edit, bool bModified)

	return true;
}

bool URootWindow::OnWindowKeyDown(EInputKey key)
{
	UpdateInputGrab();
	bool repeat = false;
	if ((int)key >= 0 && (int)key < 256)
	{
		repeat = keyDownState[key];
		keyDownState[key] = true;
	}

	if (!IsModalOpen())
		return false;

	HandleVirtualKeyboard(key, EInputType::IST_Press, repeat);
	return true;
}

bool URootWindow::OnWindowKeyUp(EInputKey key)
{
	UpdateInputGrab();
	bool wasDown = true;
	if ((int)key >= 0 && (int)key < 256)
	{
		wasDown = keyDownState[key];
		keyDownState[key] = false;
	}

	if (!IsModalOpen())
		return false;

	// To do: fire these for specific window types
	// event bool ButtonActivated(Window button)
	// event bool ToggleChanged(Window button, bool bNewToggle)
	// event bool BoxOptionSelected(Window box, int buttonNumber)
	// event bool ListRowActivated(window list, int rowId)
	// event bool ListSelectionChanged(window list, int numSelections, int focusRowId)

	if (wasDown)
		HandleVirtualKeyboard(key, EInputType::IST_Release, false);
	return true;
}

bool URootWindow::IsModalOpen()
{
	// The original counts Grab/Ungrab calls from XModalWindow::VisibilityChanged. A visible modal child gives the same answer.
	for (UWindow* child = lastChild(); child; child = child->prevSibling())
	{
		if (child->bIsVisible() && UObject::TryCast<UModalWindow>(child))
			return true;
	}
	return false;
}
