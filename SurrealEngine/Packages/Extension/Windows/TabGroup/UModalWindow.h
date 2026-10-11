#pragma once

#include "UTabGroupWindow.h"

// EMouseFocusMode of the ModalWindow script class: when the window under the mouse gets the keyboard focus
enum : uint8_t
{
	MFOCUS_None,       // never
	MFOCUS_Click,      // at a click
	MFOCUS_Enter,      // when the mouse comes onto the window
	MFOCUS_EnterLeave  // when the mouse comes onto the window. The focus goes away when the mouse leaves the window.
};

class UModalWindow : public UTabGroupWindow
{
public:
	using UTabGroupWindow::UTabGroupWindow;

	bool IsCurrentModal();
	void SetMouseFocusMode(uint8_t newFocusMode);

	void InitWindow() override;
	void DescendantRemoved(UWindow* descendant) override;

	UWindow*& acceleratorTable() { return Value<UWindow*>(PropOffsets_ModalWindow.acceleratorTable); }
	BitfieldBool bDirtyAccelerators() { return BoolValue(PropOffsets_ModalWindow.bDirtyAccelerators); }
	uint8_t& focusMode() { return Value<uint8_t>(PropOffsets_ModalWindow.focusMode); }
	UWindow*& preferredFocus() { return Value<UWindow*>(PropOffsets_ModalWindow.preferredFocus); }
	//DynamicArray& tabGroupWindowList() { return Value<DynamicArray>(PropOffsets_ModalWindow.tabGroupWindowList); }
};
