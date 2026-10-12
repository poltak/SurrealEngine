
#include "Precomp.h"
#include "UEditWindow.h"
#include "Engine.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Packages/Core/Properties/UStringProperty.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/USound.h"

// The edit functions follow XEditWindow in Extension.dll (build 1112fm):
//
// - The insertion point is a place between two characters, from 0 to the length of the text. The hook point is the
//   other end of the selected area. When the two points are at the same place, no text is selected, and selectStart
//   and selectEnd are -1.
// - SetCursorPoint moves the insertion point. Each function that moves it goes through SetCursorPoint, which keeps
//   the point in the text.
// - ReplaceText puts new text in the place of the selected area. InsertText, DeleteChar, SetText and AppendText use it.
//
// The engine has no undo steps. The text of a window keeps the form of the scripts, in which "|n" is a new line.

static bool IsWordSpace(char c)
{
	unsigned char u = (unsigned char)c;
	return (u > 8 && u < 14) || u == 32;
}

void UEditWindow::ClearUndo()
{
	// UNUSED from scripts.
	LogUnimplemented("EditWindow.ClearUndo");
}

void UEditWindow::Redo()
{
	LogUnimplemented("EditWindow.Redo");
}

void UEditWindow::Undo()
{
	LogUnimplemented("EditWindow.Undo");
}

void UEditWindow::SetMaxUndos(int newMaxUndos)
{
	maxUndos() = newMaxUndos;
}

void UEditWindow::Copy()
{
	int selStart = 0, selCount = 0;
	GetSelectedArea(selStart, selCount);
	if (selCount > 0)
	{
		engine->window->SetClipboardText(GetText().substr(selStart, selCount));
	}
	else
	{
		engine->window->SetClipboardText(GetText());
	}
}

void UEditWindow::Cut()
{
	Copy();
	int selStart = 0, selCount = 0;
	GetSelectedArea(selStart, selCount);
	if (selCount > 0)
		DeleteChar(false, true);
}

void UEditWindow::Paste()
{
	InsertText(engine->window->GetClipboardText(), true, {});
}

// As XEditWindow::DeleteChar and DeleteText: delete the selected area. With no selected area, delete the character
// before or after the insertion point, if there is one.
void UEditWindow::DeleteChar(std::optional<bool> bBefore, std::optional<bool> bUndo)
{
	if (!bUndo || !*bUndo)
		SetTextChangedFlag(true);

	if (selectEnd() <= selectStart())
	{
		int pos = insertPos();
		bool hasChar;
		if (bBefore && *bBefore)
		{
			hasChar = pos >= 1;
			pos--;
		}
		else
		{
			hasChar = pos < (int)Text().size();
		}
		if (hasChar)
		{
			selectStart() = pos;
			selectEnd() = pos + 1;
		}
	}

	ReplaceText(std::string(), false);
}

void UEditWindow::EnableEditing(std::optional<bool> bEdit)
{
	bEditable() = bEdit ? *bEdit : true;
}

void UEditWindow::EnableSingleLineEditing(std::optional<bool> bSingle)
{
	bSingleLine() = bSingle ? *bSingle : true;
}

void UEditWindow::EnableUppercaseOnly(std::optional<bool> bUppercase)
{
	bUppercaseOnly() = bUppercase ? *bUppercase : true;
}

int UEditWindow::GetInsertionPoint()
{
	return insertPos();
}

// As XEditWindow::GetSelectedArea. The area is always in the text, so that a caller can cut the text with it.
void UEditWindow::GetSelectedArea(int& startPos, int& Count)
{
	int length = (int)Text().size();
	int start = std::clamp(selectStart(), 0, length);
	int end = std::clamp(selectEnd(), start, length);
	startPos = start;
	Count = end - start;
}

bool UEditWindow::HasTextChanged()
{
	return textChanged;
}

// As XEditWindow::SetText: the new text replaces all of the old text, and the insertion point goes to the start
void UEditWindow::SetText(const std::string& NewText)
{
	int length = (int)Text().size();
	selectStart() = 0;
	selectEnd() = length;
	insertPos() = length;
	InsertText(NewText, false, false);
	SetInsertionPoint(0, false);
}

// As XEditWindow::AppendText: the new text goes to the end, and the insertion point goes to the start
void UEditWindow::AppendText(const std::string& NewText)
{
	int length = (int)Text().size();
	selectStart() = length;
	selectEnd() = length;
	insertPos() = length;
	InsertText(NewText, false, false);
	SetInsertionPoint(0, false);
}

// As XEditWindow::InsertText: the text goes through the filter and then replaces the selected area. Returns false
// when the filter refuses all of the text.
bool UEditWindow::InsertText(std::optional<std::string> InsertText, std::optional<bool> bUndo, std::optional<bool> bSelect)
{
	std::string filtered;
	if (!FilterString(InsertText ? *InsertText : std::string(), filtered))
		return false;

	if (!bUndo || !*bUndo)
		SetTextChangedFlag(true);

	ReplaceText(filtered, bSelect && *bSelect);
	return true;
}

// As XEditWindow::FilterString: a field with one line takes no new line, and FilterChar can change or refuse each
// character. Returns false when the text has characters and none of them stays.
bool UEditWindow::FilterString(const std::string& text, std::string& result)
{
	result.clear();
	bool singleLine = bSingleLine();
	for (size_t i = 0; i < text.size(); i++)
	{
		// The script of EditWindow sends "|n" for the Enter key
		if (text[i] == '|' && i + 1 < text.size() && text[i + 1] == 'n')
		{
			if (!singleLine)
				result += "|n";
			i++;
			continue;
		}
		if (text[i] == '\n' && singleLine)
			continue;

		std::string ch(1, text[i]);
		if (FilterChar(ch))
			result += ch;
	}
	return text.empty() || !result.empty();
}

// As XEditWindow::FilterChar: a field for uppercase text changes the letter. Then the script function FilterChar of
// the window, if it has one, can change the character or refuse it.
bool UEditWindow::FilterChar(std::string& ch)
{
	if (bUppercaseOnly())
	{
		for (char& c : ch)
		{
			if (c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
		}
	}

	UFunction* func = FindEventFunction(this, "FilterChar");
	if (!func)
		return true;
	return Frame::Call(func, this, Array<ExpressionValue>{ ExpressionValue::Variable(&ch, engine->stringprop) }).ToBool();
}

// As XEditWindow::ReplaceText: put the new text in the place of the selected area, or at the insertion point when no
// text is selected. The text does not get longer than the size limit. After the change the insertion point is behind
// the new text. With the select flag the new text is the selected area.
void UEditWindow::ReplaceText(const std::string& newText, bool select)
{
	std::string text = Text();
	int length = (int)text.size();
	int selCount = selectStart() < selectEnd() ? selectEnd() - selectStart() : 0;
	int newCount = (int)newText.size();

	int limit = maxSize();
	if (limit > 0)
	{
		if (limit < length)
		{
			// The text is longer than the limit. Cut it and do no more.
			text.resize((size_t)limit);
			Text() = text;
			SetTextChangedFlag(true);
			AskParentForReconfigure();
			SetCursorPoint(0, false);
			return;
		}
		if (selCount < newCount)
		{
			int over = length - limit - selCount + newCount;
			if (over > 0)
			{
				newCount -= over;
				if (newCount < 0)
					return;
			}
		}
	}

	if (selCount < 1 && newCount < 1)
		return;

	if (selectStart() < selectEnd())
		insertPos() = selectStart();

	int pos = std::clamp(insertPos(), 0, length);
	selCount = std::min(selCount, length - pos);
	text.replace((size_t)pos, (size_t)selCount, newText, 0, (size_t)newCount);
	Text() = text;
	textChanged = true;

	ChangeText();
	AskParentForReconfigure();

	insertPos() = pos;
	insertHookPos() = pos;
	SetCursorPoint(pos + newCount, select);
}

// As XEditWindow::ChangeText: this window and the windows above it get the TextChanged event, until one handles it
void UEditWindow::ChangeText()
{
	bool modified = HasTextChanged();
	for (UWindow* cur = this; cur != nullptr; cur = cur->parentOwner())
	{
		if (cur->TextChanged(this, modified))
			break;
	}
}

bool UEditWindow::IsEditingEnabled()
{
	return bEditable();
}

bool UEditWindow::IsSingleLineEditingEnabled()
{
	return bSingleLine();
}

// As XEditWindow::MoveInsertionPoint. The engine does not have the rows of a text with more than one line, so the
// moves by row and by page do nothing.
void UEditWindow::MoveInsertionPoint(uint8_t moveInsert, std::optional<bool> bDrag)
{
	bool drag = bDrag && *bDrag;
	switch ((EMoveInsert)moveInsert)
	{
	case EMoveInsert::Left:
		SetCursorPoint(insertPos() - 1, drag);
		break;
	case EMoveInsert::Right:
		SetCursorPoint(insertPos() + 1, drag);
		break;
	case EMoveInsert::WordLeft:
		SetCursorPoint(PrevWord(insertPos()), drag);
		break;
	case EMoveInsert::WordRight:
		SetCursorPoint(NextWord(insertPos()), drag);
		break;
	case EMoveInsert::StartOfLine:
		SetCursorPoint(StartOfLine(insertPos()), drag);
		break;
	case EMoveInsert::EndOfLine:
		SetCursorPoint(EndOfLine(insertPos()), drag);
		break;
	case EMoveInsert::Home:
		SetCursorPoint(0, drag);
		break;
	case EMoveInsert::End:
		SetCursorPoint((int)Text().size(), drag);
		break;
	case EMoveInsert::Up:
	case EMoveInsert::Down:
	case EMoveInsert::PageUp:
	case EMoveInsert::PageDown:
		// To do: need multiline support
		break;
	}
}

void UEditWindow::SetInsertionPoint(int NewPos, std::optional<bool> bDrag)
{
	SetCursorPoint(NewPos, bDrag && *bDrag);
}

// As XEditWindow::SetCursorPoint: the insertion point stays in the text. Without the drag flag the hook point comes
// with it, and then no text is selected.
void UEditWindow::SetCursorPoint(int newPos, bool drag)
{
	newPos = std::clamp(newPos, 0, (int)Text().size());
	insertPos() = newPos;
	if (!drag)
		insertHookPos() = newPos;
	ComputeSelectArea();
	ShowCursor();
}

// As XEditWindow::ComputeSelectArea: the selected area is the text between the hook point and the insertion point
void UEditWindow::ComputeSelectArea()
{
	int length = (int)Text().size();
	int pos = insertPos();
	int hook = std::clamp(insertHookPos(), 0, length);
	insertHookPos() = hook;
	if (pos == hook)
	{
		selectStart() = -1;
		selectEnd() = -1;
	}
	else
	{
		selectStart() = std::min(pos, hook);
		selectEnd() = std::max(pos, hook);
	}
}

// As XEditWindow::ShowCursor: the cursor shows at once, and a clip window above this window brings it into view.
// The engine gives a window its new size when it draws the window tree, so the request to the clip window waits for
// the next draw of this window. The field then has the size of its new text.
void UEditWindow::ShowCursor()
{
	blinkDelay() = 0.0f;
	showCursorPending = true;
}

// As in XWindow::AskParentToShowArea, the request is only for the part of the area that is in the window
void UEditWindow::ShowCursorArea()
{
	if (!bSingleLine())
		return;

	float left = std::max(PosToX(insertPos()) - 1.0f, 0.0f);
	float right = std::min(left + 3.0f, Width());
	if (right > left && Height() > 0.0f)
		AskParentToShowArea(left, 0.0f, right - left, Height());
}

// The place of the insertion point in a field with one line, in units from the left edge of the text
float UEditWindow::PosToX(int pos)
{
	pos = std::min(pos, (int)Text().size());
	if (pos <= 0 || !engine->dxgc || !normalFont())
		return 0.0f;

	UGC* gc = engine->dxgc;
	UObject* oldNormalFont = nullptr;
	UObject* oldBoldFont = nullptr;
	gc->GetFonts(oldNormalFont, oldBoldFont);
	bool oldWordWrap = gc->bWordWrap();
	gc->SetFonts(normalFont(), boldFont());
	gc->bWordWrap() = false;

	float xExtent = 0.0f, yExtent = 0.0f;
	gc->GetTextExtent(100000.0f, xExtent, yExtent, Text().substr(0, (size_t)pos));

	gc->bWordWrap() = oldWordWrap;
	gc->SetFonts(oldNormalFont, oldBoldFont);
	return std::max(xExtent - 2.0f, 0.0f);
}

// The place between two characters that is nearest to a point, in a field with one line
int UEditWindow::XToPos(float x)
{
	int length = (int)Text().size();
	float prev = 0.0f;
	for (int i = 1; i <= length; i++)
	{
		float cur = PosToX(i);
		if (x < (prev + cur) * 0.5f)
			return i - 1;
		prev = cur;
	}
	return length;
}

// As XEditWindow::NextWord: the start of the next word behind a space
int UEditWindow::NextWord(int pos)
{
	const std::string& text = Text();
	int length = (int)text.size();
	bool space = false;
	for (int i = std::max(pos, 0); i < length; i++)
	{
		if (IsWordSpace(text[i]))
			space = true;
		else if (space)
			return i;
	}
	return length;
}

// As XEditWindow::PrevWord: the start of the word at or before the place
int UEditWindow::PrevWord(int pos)
{
	const std::string& text = Text();
	int i = std::min(pos, (int)text.size());
	bool word = false;
	while (i > 0)
	{
		if (!IsWordSpace(text[i - 1]))
			word = true;
		else if (word)
			break;
		i--;
	}
	return i;
}

int UEditWindow::StartOfLine(int pos)
{
	const std::string& text = Text();
	for (int i = std::min(pos, (int)text.size()); i > 0; i--)
	{
		if (text[i - 1] == '\n')
			return i;
		if (i >= 2 && text[i - 2] == '|' && text[i - 1] == 'n')
			return i;
	}
	return 0;
}

int UEditWindow::EndOfLine(int pos)
{
	const std::string& text = Text();
	int length = (int)text.size();
	for (int i = std::max(pos, 0); i < length; i++)
	{
		if (text[i] == '\n')
			return i;
		if (text[i] == '|' && i + 1 < length && text[i + 1] == 'n')
			return i;
	}
	return length;
}

void UEditWindow::PlayEditSound(UObject* sound, std::optional<float> Volume, std::optional<float> Pitch)
{
	// What is the difference between this function and UWindow::PlaySound?
	PlaySound(sound, Volume, Pitch, {}, {});
}

void UEditWindow::SetEditCursor(std::optional<UObject*> newCursor, std::optional<UObject*> newCursorShadow, std::optional<Color> NewColor)
{
	// As XEditWindow::execSetEditCursor: an argument that the script leaves out gets its default
	editCursor() = newCursor ? UObject::Cast<UTexture>(*newCursor) : nullptr;
	editCursorShadow() = newCursorShadow ? UObject::Cast<UTexture>(*newCursorShadow) : nullptr;
	editCursorColor() = NewColor ? *NewColor : Color{ 255, 255, 255, 255 };
}

// As XEditWindow::CursorRequested: a field that the player can edit shows its text cursor, or the text cursor of
// the root window when the field has none
UTexture* UEditWindow::CursorRequested(UWindow* win, float pointX, float pointY, float& hotX, float& hotY, Color& newColor, UTexture*& shadowTexture)
{
	if (bEditable())
	{
		UTexture* cursor = editCursor();
		shadowTexture = editCursorShadow();
		newColor = editCursorColor();
		if (!cursor)
		{
			URootWindow* root = GetRootWindow();
			cursor = root ? root->defaultEditCursor() : nullptr;
		}
		if (cursor)
			return cursor;
	}
	return ULargeTextWindow::CursorRequested(win, pointX, pointY, hotX, hotY, newColor, shadowTexture);
}

void UEditWindow::SetEditSounds(std::optional<UObject*> newTypeSound, std::optional<UObject*> newDeleteSound, std::optional<UObject*> newEnterSound, std::optional<UObject*> newMoveSound)
{
	if (newTypeSound.has_value())
		typeSound() = UObject::Cast<USound>(newTypeSound.value());
	if (newDeleteSound.has_value())
		deleteSound() = UObject::Cast<USound>(newDeleteSound.value());
	if (newEnterSound.has_value())
		enterSound() = UObject::Cast<USound>(newEnterSound.value());
	if (newMoveSound.has_value())
		moveSound() = UObject::Cast<USound>(newMoveSound.value());
}

void UEditWindow::SetInsertionPointBlinkRate(std::optional<float> newBlinkStart, std::optional<float> newBlinkPeriod)
{
	if (newBlinkStart.has_value())
		blinkStart() = newBlinkStart.value();
	if (newBlinkPeriod.has_value())
		blinkPeriod() = newBlinkPeriod.value();
}

void UEditWindow::SetInsertionPointTexture(std::optional<UObject*> NewTexture, std::optional<Color> NewColor)
{
	if (NewTexture.has_value())
		insertTexture() = UObject::Cast<UTexture>(NewTexture.value());
	if (NewColor.has_value())
		insertColor() = NewColor.value();
}

void UEditWindow::SetInsertionPointType(uint8_t newType, std::optional<float> prefWidth, std::optional<float> prefHeight)
{
	insertType() = newType;
	if (prefWidth.has_value())
		insertPrefWidth() = prefWidth.value();
	if (prefHeight.has_value())
		insertPrefHeight() = prefHeight.value();
}

// As XEditWindow::SetMaxSize: a text that is longer than the new limit loses its end
void UEditWindow::SetMaxSize(int newMaxSize)
{
	if (maxSize() == newMaxSize)
		return;
	maxSize() = newMaxSize;

	int length = (int)Text().size();
	if (newMaxSize > 0 && newMaxSize < length)
	{
		selectStart() = newMaxSize;
		selectEnd() = length;
		insertPos() = length;
		InsertText({}, false, false);
		SetInsertionPoint(0, false);
	}
}

// As XEditWindow::SetSelectedArea
void UEditWindow::SetSelectedArea(int startPos, int Count)
{
	SetCursorPoint(startPos, false);
	SetCursorPoint(startPos + Count, true);
}

void UEditWindow::SetSelectedAreaTextColor(std::optional<Color> NewColor)
{
	if (NewColor.has_value())
		selectColor() = NewColor.value();
}

void UEditWindow::SetSelectedAreaTexture(std::optional<UObject*> NewTexture, std::optional<Color> NewColor)
{
	if (NewTexture.has_value())
		selectTexture() = UObject::Cast<UTexture>(NewTexture.value());
	if (NewColor.has_value())
		selectColor() = NewColor.value();
}

void UEditWindow::ClearTextChangedFlag()
{
	textChanged = false;
}

void UEditWindow::SetTextChangedFlag(std::optional<bool> bSet)
{
	textChanged = bSet ? *bSet : true;
}

void UEditWindow::InitWindow()
{
	// As XEditWindow::Init: a new field can be edited, has more than one line, and can get the keyboard focus.
	// Set this before the InitWindow event of the script runs. The script of a field with one line calls
	// EnableSingleLineEditing in that event.
	blinkPeriod() = 0.5f;
	bEditable() = true;
	bSingleLine() = false;
	insertPos() = 0;
	insertHookPos() = 0;
	selectStart() = 0;
	selectEnd() = 0;
	maxSize() = 200000;
	SetSelectability(true);

	ULargeTextWindow::InitWindow();
}

void UEditWindow::Tick(float timeElapsed)
{
	ULargeTextWindow::Tick(timeElapsed);

	blinkDelay() -= timeElapsed;
	if (blinkDelay() < -blinkPeriod())
		blinkDelay() = blinkPeriod();
}

void UEditWindow::DrawWindow(UGC* gc)
{
	if (showCursorPending)
	{
		showCursorPending = false;
		ShowCursorArea();
	}

	UWindow::DrawWindow(gc);

	gc->SetFont(normalFont());
	gc->SetAlignments(HAlign(), VAlign());
	gc->bWordWrap() = !bSingleLine();

	float w = Width();
	float h = Height();

	int selStart = 0, selCount = 0;
	GetSelectedArea(selStart, selCount);
	if (selCount > 0)
	{
		// To do: this doesn't work for multi line edit (we must split on a per line basis)

		std::string beforeText = Text().substr(0, selStart);
		std::string selectionText = Text().substr(selStart, selCount);
		std::string afterText = Text().substr(selStart + selCount);

		float x = 0.0f;
		float xExtent = 0.0f, yExtent = 0.0f;
		gc->SetTextColor(TextColor());
		gc->DrawText(x, 0.0f, w - x, h, beforeText);
		gc->GetTextExtent(w - x, xExtent, yExtent, beforeText);
		x += xExtent - 1.0f;

		gc->GetTextExtent(w - x, xExtent, yExtent, selectionText);
		if (auto tex = selectTexture())
		{
			gc->SetTileColor(selectColor());
			gc->DrawStretchedTexture(x, 0.0f, xExtent - 1.0f, h, 0.0f, 0.0f, (float)tex->USize(), (float)tex->VSize(), tex);
		}
		gc->SetTextColor(TextColor());
		gc->DrawText(x, 0.0f, w - x, h, selectionText);
		x += xExtent - 1.0f;

		gc->SetTextColor(TextColor());
		gc->DrawText(x, 0.0f, w - x, h, afterText);
	}
	else
	{
		gc->SetTextColor(TextColor());
		gc->DrawText(0.0f, 0.0f, Width(), Height(), Text());
	}

	if (IsFocusWindow() && blinkDelay() < 0.0f)
	{
		if (auto tex = insertTexture())
		{
			float xExtent = 0.0f, yExtent = 0.0f;
			std::string beforeInsertPoint = Text().substr(0, (size_t)std::clamp(insertPos(), 0, (int)Text().size()));
			gc->GetTextExtent(w, xExtent, yExtent, beforeInsertPoint);
			float x = std::max(xExtent - 2.0f, 0.0f);

			// Only EInsertionPointType::Insert is in use according to the scripts
			// EInsertionPointType type = (EInsertionPointType)insertType();

			gc->SetTileColor(insertColor());
			gc->DrawStretchedTexture(x, 0.0f, 1.0f, h, 0.0f, 0.0f, (float)tex->USize(), (float)tex->VSize(), tex);
		}
	}

	// DrawDebugBox(gc);
}

// As XEditWindow::KeyPressed: the script gets the event. Then a field that the player can edit takes the character,
// but not the two characters of the console key.
bool UEditWindow::KeyPressed(std::string key)
{
	bool handled = ULargeTextWindow::KeyPressed(key);

	if (IsEditingEnabled() && !key.empty() && key != "`" && key != "~" && (unsigned char)key.front() >= 32)
	{
		if (InsertText(key, true, false))
		{
			PlayEditSound(typeSound(), {}, {});
			handled = true;
		}
	}

	return handled;
}

bool UEditWindow::VirtualKeyPressed(EInputKey key, bool bRepeat)
{
	// As XEditWindow::VirtualKeyPressed: the Enter key in a field with one line tells the windows above this
	// field that the player finished the text. A login screen then goes to the next field or starts the login.
	// The script does not get the key in that case.
	if (key == IK_Enter)
	{
		PlayEditSound(enterSound(), {}, {});
		if (bSingleLine() && bEditable())
		{
			ActivateEdit();
			return true;
		}
	}

	return ULargeTextWindow::VirtualKeyPressed(key, bRepeat);
}

void UEditWindow::ActivateEdit()
{
	bool modified = HasTextChanged();
	for (UWindow* cur = this; cur != nullptr; cur = cur->parentOwner())
	{
		if (cur->EditActivated(this, modified))
			break;
	}
}

bool UEditWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	// Other buttons go to the windows above. The mouse wheel then moves the scroll area of the field.
	if (button != IK_LeftMouse)
		return ULargeTextWindow::MouseButtonPressed(pointX, pointY, button, numClicks);

	SetFocusWindow(this);
	ULargeTextWindow::MouseButtonPressed(pointX, pointY, button, numClicks);

	// As XEditWindow::MouseButtonPressed: the insertion point goes to the place of the click. With the Shift key down,
	// the text from the old place to the new place is the selected area.
	if (bEditable())
		SetInsertionPoint(bSingleLine() ? XToPos(pointX) : insertPos(), IsKeyDown(IK_Shift));
	return true;
}

bool UEditWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	if (button != IK_LeftMouse)
		return ULargeTextWindow::MouseButtonReleased(pointX, pointY, button, numClicks);

	ULargeTextWindow::MouseButtonReleased(pointX, pointY, button, numClicks);
	return true;
}
