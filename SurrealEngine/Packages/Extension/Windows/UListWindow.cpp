
#include "Precomp.h"
#include "UListWindow.h"
#include "Engine.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Extension/Windows/UGC.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// The list control follows XListWindow in Extension.dll (build 1112fm):
//
// - A row has one field for each column. A field of a float or time column also has a value, and its text
//   is the value in the format of the column.
// - The rows stay in sort order when auto sort is on. The sort keys are the columns in descending sort order
//   number, then the position of the row.
// - The preferred size is the sum of the visible column widths and the number of rows times the line size.
//   A scroll area uses it, so a list without this size shows no rows.
// - focusLine and anchorLine hold a row id. Zero means no row.

namespace
{
	float FontHeight(UFont* font)
	{
		return font ? (float)font->GetGlyph(' ').VSize : 0.0f;
	}

	float TextWidth(UFont* font, const std::string& text)
	{
		float width = 0.0f;
		if (font)
		{
			for (char c : text)
				width += (float)font->GetGlyph(c).USize;
		}
		return width;
	}

	char LowerChar(char c)
	{
		return (c >= 'A' && c <= 'Z') ? (char)(c + 0x20) : c;
	}

	int CompareText(const std::string& a, const std::string& b, bool caseSensitive)
	{
		size_t count = std::min(a.size(), b.size());
		for (size_t i = 0; i < count; i++)
		{
			unsigned char ca = (unsigned char)(caseSensitive ? a[i] : LowerChar(a[i]));
			unsigned char cb = (unsigned char)(caseSensitive ? b[i] : LowerChar(b[i]));
			if (ca != cb)
				return ca < cb ? -1 : 1;
		}
		if (a.size() == b.size())
			return 0;
		return a.size() < b.size() ? -1 : 1;
	}

	// XListWindow::StringToFloat. It reads a number in base 10, base 8 (a zero at the start) or base 16 ("0x").
	// The separator ' adds hours and the separators : and " add minutes, so "1:30" is 90.
	float StringToFloat(const std::string& text)
	{
		float total = 0.0f;
		float current = 0.0f;
		float sign = 1.0f;
		float base = 10.0f;
		float fraction = 1.0f;
		bool inFraction = false;
		bool signAllowed = true;
		bool spaceAllowed = true;
		bool atStart = true;

		for (size_t i = 0; i < text.size(); i++)
		{
			unsigned int c = (unsigned char)text[i];
			int digit = -1;

			if (c >= '0' && c <= '9')
			{
				if (c == '0' && atStart)
				{
					base = 8.0f;
					if (i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X'))
						base = 16.0f;
				}
				else
				{
					digit = (int)(c - '0');
				}
			}
			else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
			{
				if (c >= 'a')
					c -= 0x20;
				digit = (int)(c - 0x37);
			}
			else if (((c >= 9 && c <= 13) || c == ' ') && spaceAllowed)
			{
				continue;
			}
			else if ((c == '+' || c == '-') && signAllowed)
			{
				if (c == '-')
					sign = -1.0f;
				signAllowed = false;
				spaceAllowed = false;
				continue;
			}
			else if (c == '.' || c == ',')
			{
				inFraction = true;
			}
			else if (c == '\'' || c == ':' || c == '"')
			{
				total += current * (c == '\'' ? 3600.0f : 60.0f) * sign;
				current = 0.0f;
				inFraction = false;
				signAllowed = false;
				spaceAllowed = true;
				atStart = false;
				continue;
			}
			else
			{
				break;
			}

			if (digit >= 0)
			{
				if ((float)digit >= base)
					break;
				if (inFraction)
				{
					fraction *= base;
					current += (float)digit / fraction;
				}
				else
				{
					current = current * base + (float)digit;
				}
			}

			signAllowed = false;
			atStart = false;
			spaceAllowed = false;
		}

		return current * sign + total;
	}

	std::string FormatValue(const std::string& format, float value)
	{
		char buffer[256];
		// The format comes from a script. Use it only when it has one float conversion and nothing that reads memory.
		int conversions = 0;
		bool safe = true;
		for (size_t i = 0; i < format.size(); i++)
		{
			if (format[i] != '%')
				continue;
			if (i + 1 < format.size() && format[i + 1] == '%')
			{
				i++;
				continue;
			}
			size_t j = i + 1;
			while (j < format.size() && (std::isdigit((unsigned char)format[j]) || format[j] == '.' || format[j] == '-' || format[j] == '+' || format[j] == ' ' || format[j] == '0'))
				j++;
			if (j >= format.size() || (format[j] != 'f' && format[j] != 'g' && format[j] != 'e' && format[j] != 'F' && format[j] != 'G' && format[j] != 'E'))
				safe = false;
			conversions++;
			i = j;
		}
		if (!safe || conversions != 1)
			snprintf(buffer, sizeof(buffer), "%f", (double)value);
		else
			snprintf(buffer, sizeof(buffer), format.c_str(), (double)value);
		return buffer;
	}

	bool IsHotKeyValid(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}
}

void UListWindow::InitWindow()
{
	Delimiter() = ";";
	inverseColor() = { 0, 0, 0, 255 };
	highlightColor() = { 255, 255, 255, 255 };
	focusColor() = { 128, 128, 128, 255 };
	focusThickness() = 1.0f;
	bAutoSort() = false;
	bAutoExpandColumns() = true;
	bMultiSelect() = true;
	colMargin() = 3.0f;
	rowMargin() = 1.0f;
	bHotKeys() = true;
	hotKeyCol() = 0;
	numSelected() = 0;
	focusLine() = 0;
	anchorLine() = 0;
	bDragging() = false;
	lastIndex() = 0;
	remainingDelay() = 0.0f;
	hotKeyString().clear();
	hotKeyTimer() = 0.0f;
	lineSize() = 0.0f;

	SetNumColumns(1);
	ResetSortColumns(true);
	SetSelectability(true);

	UWindow::InitWindow();
}

/////////////////////////////////////////////////////////////////////////////
// Rows and fields

UListWindow::Row* UListWindow::FindRow(int rowId)
{
	if (rowId == 0)
		return nullptr;
	auto it = rowsById.find(rowId);
	return it != rowsById.end() ? it->second.get() : nullptr;
}

int UListWindow::IndexToRowId(int index)
{
	if (index < 0 || (size_t)index >= rows.size())
		return 0;
	return rows[index]->id;
}

int UListWindow::RowIdToIndex(int rowId)
{
	Row* row = FindRow(rowId);
	return row ? row->index : -1;
}

void UListWindow::RecomputeIndices(size_t first)
{
	for (size_t i = first; i < rows.size(); i++)
		rows[i]->index = (int)i;
}

void UListWindow::ExtendRow(Row* row)
{
	row->fields.resize(columns.size());
}

void UListWindow::ComputeFieldSize(Row* row, int colIndex)
{
	Field& field = row->fields[colIndex];
	UFont* font = columns[colIndex].font;
	if (font)
	{
		field.width = TextWidth(font, field.text);
		field.height = FontHeight(font);
	}
	else
	{
		field.width = 0.0f;
		field.height = 0.0f;
	}
}

bool UListWindow::ExpandColumnByField(Row* row, int colIndex)
{
	float width = colMargin() * 2.0f + row->fields[colIndex].width;
	if (columns[colIndex].width < width)
	{
		columns[colIndex].width = width;
		return true;
	}
	return false;
}

void UListWindow::SetFieldByString(Row* row, int colIndex, const std::string& text)
{
	const Column& col = columns[colIndex];
	Field& field = row->fields[colIndex];
	if (col.type == 0)
	{
		field.value = 0.0f;
		field.text = text;
	}
	else
	{
		field.value = (col.type == 1 || col.type == 2) ? StringToFloat(text) : 0.0f;
		field.text = FormatValue(col.type == 1 ? col.format : std::string("%f"), field.value);
	}
}

void UListWindow::SetFieldByValue(Row* row, int colIndex, float value)
{
	const Column& col = columns[colIndex];
	Field& field = row->fields[colIndex];
	field.text = FormatValue(col.type == 1 ? col.format : std::string("%f"), value);
	field.value = col.type == 0 ? 0.0f : value;
}

// Puts the text of a row string into the fields. Returns true if a column got wider.
bool UListWindow::FillRow(Row* row, const std::string& rowStr)
{
	bool expanded = false;
	ExtendRow(row);

	char delimiter = Delimiter().empty() ? ';' : Delimiter()[0];
	size_t pos = 0;
	bool hasText = true;
	for (size_t i = 0; i < columns.size(); i++)
	{
		std::string text;
		if (hasText)
		{
			size_t end = rowStr.find(delimiter, pos);
			if (end == std::string::npos)
			{
				text = rowStr.substr(pos);
				hasText = false;
			}
			else
			{
				text = rowStr.substr(pos, end - pos);
				pos = end + 1;
			}
		}

		SetFieldByString(row, (int)i, text);
		ComputeFieldSize(row, (int)i);
		if (bAutoExpandColumns() && ExpandColumnByField(row, (int)i))
			expanded = true;
	}
	return expanded;
}

int UListWindow::AddRow(const std::string& rowStr, std::optional<int> clientData)
{
	auto newRow = std::make_unique<Row>();
	Row* row = newRow.get();
	row->id = nextRowId++;
	row->clientInt = clientData ? *clientData : 0;
	rowsById[row->id] = std::move(newRow);

	FillRow(row, rowStr);
	InsertRow(row);
	AskParentForReconfigure();
	return row->id;
}

void UListWindow::InsertRow(Row* row)
{
	if (!bAutoSort())
	{
		row->index = (int)rows.size();
		rows.push_back(row);
		return;
	}

	int position = 0;
	if (FindRowData(row, position))
		position++;
	rows.insert(rows.begin() + position, row);
	RecomputeIndices(position);
}

void UListWindow::RemoveRow(Row* row)
{
	if (row->index < 0 || (size_t)row->index >= rows.size() || rows[row->index] != row)
		return;
	size_t index = (size_t)row->index;
	rows.erase(rows.begin() + index);
	row->index = -1;
	RecomputeIndices(index);
}

void UListWindow::DeleteRow(int rowId)
{
	Row* row = FindRow(rowId);
	if (!row)
		return;

	bool changed = false;
	ChangeSelectRow(row->index, false, changed);
	RemoveRow(row);
	if (focusLine() == rowId)
		focusLine() = 0;
	if (anchorLine() == rowId)
		anchorLine() = 0;
	rowsById.erase(rowId);

	if (changed)
		ChangeListSelection();
	AskParentForReconfigure();
}

void UListWindow::DeleteAllRows()
{
	bool changed = numSelected() > 0;
	rows.clear();
	rowsById.clear();
	focusLine() = 0;
	anchorLine() = 0;
	numSelected() = 0;
	if (changed)
		ChangeListSelection();
	AskParentForReconfigure();
}

void UListWindow::ModifyRow(int rowId, const std::string& rowStr)
{
	Row* row = FindRow(rowId);
	if (!row)
		return;
	bool expanded = FillRow(row, rowStr);
	CheckPosition(row);
	if (expanded)
		AskParentForReconfigure();
}

void UListWindow::SetField(int rowId, int colIndex, const std::string& fieldStr)
{
	Row* row = FindRow(rowId);
	if (!row || !IsColumnValid(colIndex))
		return;
	SetFieldByString(row, colIndex, fieldStr);
	ComputeFieldSize(row, colIndex);
	bool expanded = bAutoExpandColumns() && ExpandColumnByField(row, colIndex);
	CheckPosition(row);
	if (expanded)
		AskParentForReconfigure();
}

std::string UListWindow::GetField(int rowId, int colIndex)
{
	Row* row = FindRow(rowId);
	if (!row || !IsColumnValid(colIndex))
		return {};
	return row->fields[colIndex].text;
}

void UListWindow::SetFieldValue(int rowId, int colIndex, float NewValue)
{
	Row* row = FindRow(rowId);
	if (!row || !IsColumnValid(colIndex))
		return;
	SetFieldByValue(row, colIndex, NewValue);
	ComputeFieldSize(row, colIndex);
	bool expanded = bAutoExpandColumns() && ExpandColumnByField(row, colIndex);
	CheckPosition(row);
	if (expanded)
		AskParentForReconfigure();
}

float UListWindow::GetFieldValue(int rowId, int colIndex)
{
	Row* row = FindRow(rowId);
	if (!row || !IsColumnValid(colIndex))
		return 0.0f;
	return row->fields[colIndex].value;
}

void UListWindow::SetRowClientInt(int rowId, int clientInt)
{
	if (Row* row = FindRow(rowId))
		row->clientInt = clientInt;
}

int UListWindow::GetRowClientInt(int rowId)
{
	Row* row = FindRow(rowId);
	return row ? row->clientInt : 0;
}

void UListWindow::SetRowClientObject(int rowId, UObject* clientObj)
{
	if (Row* row = FindRow(rowId))
		row->clientObj = clientObj;
}

UObject* UListWindow::GetRowClientObject(int rowId)
{
	Row* row = FindRow(rowId);
	return row ? row->clientObj : nullptr;
}

int UListWindow::GetNumRows()
{
	return (int)rows.size();
}

/////////////////////////////////////////////////////////////////////////////
// Sort

std::vector<const UListWindow::Column*> UListWindow::SortedColumns() const
{
	// The columns with a sort order number of zero or more, highest number first.
	// Columns with the same number stay in column order.
	std::vector<const Column*> result;
	for (const Column& col : columns)
	{
		if (col.sortOrder < 0)
			continue;
		auto it = result.begin();
		while (it != result.end() && (*it)->sortOrder >= col.sortOrder)
			++it;
		result.insert(it, &col);
	}
	return result;
}

int UListWindow::CompareRows(const Row* a, const Row* b, const std::vector<const Column*>& sortCols) const
{
	for (const Column* col : sortCols)
	{
		size_t colIndex = (size_t)(col - columns.data());
		const Field& fa = a->fields[colIndex];
		const Field& fb = b->fields[colIndex];

		int result = 0;
		if (col->type == 1 || col->type == 2)
		{
			if (fa.value < fb.value)
				result = -1;
			else if (fa.value > fb.value)
				result = 1;
		}
		else
		{
			result = CompareText(fa.text, fb.text, col->caseSensitive);
		}

		if (result != 0)
			return col->reverse ? -result : result;
	}

	// Rows with the same keys keep their order. A new row has the position -1, so it goes before them.
	if (a->index < b->index)
		return -1;
	if (b->index < a->index)
		return 1;
	return 0;
}

// Finds the position of a row in the sorted list. Returns true if a row with the same keys and position is there.
bool UListWindow::FindRowData(const Row* row, int& position) const
{
	auto sortCols = SortedColumns();
	int low = 0;
	int high = (int)rows.size();
	bool found = false;
	while (low != high)
	{
		int middle = (low + high) >> 1;
		int result = CompareRows(rows[middle], row, sortCols);
		if (result == 0)
		{
			found = true;
			low = middle;
			high = middle;
			break;
		}
		if (result < 0)
			low = middle + 1;
		else
			high = middle;
	}
	position = (low + high) >> 1;
	return found;
}

void UListWindow::SortRows()
{
	auto sortCols = SortedColumns();
	if (rows.size() > 1)
	{
		std::stable_sort(rows.begin(), rows.end(), [&](const Row* a, const Row* b) { return CompareRows(a, b, sortCols) < 0; });
	}
	RecomputeIndices();
}

// Moves a row to its place when one of its fields changed
void UListWindow::CheckPosition(Row* row)
{
	if (!bAutoSort() || row->index < 0)
		return;

	auto sortCols = SortedColumns();
	bool beforeOk = row->index < 1 || CompareRows(rows[row->index - 1], row, sortCols) < 0;
	bool afterOk = (size_t)row->index + 1 >= rows.size() || CompareRows(row, rows[row->index + 1], sortCols) < 0;
	if (beforeOk && afterOk)
		return;

	RemoveRow(row);
	InsertRow(row);
}

void UListWindow::SetSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive)
{
	if (!IsColumnValid(colIndex))
		return;
	for (Column& col : columns)
	{
		col.sortOrder = 0;
		col.reverse = false;
		col.caseSensitive = false;
	}
	columns[colIndex].sortOrder = 1;
	columns[colIndex].reverse = bReverse ? *bReverse : false;
	columns[colIndex].caseSensitive = bCaseSensitive ? *bCaseSensitive : false;
	if (bAutoSort())
		SortRows();
}

void UListWindow::AddSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive)
{
	if (!IsColumnValid(colIndex))
		return;
	for (Column& col : columns)
	{
		if (col.sortOrder > 0)
			col.sortOrder++;
	}
	columns[colIndex].sortOrder = 1;
	columns[colIndex].reverse = bReverse ? *bReverse : false;
	columns[colIndex].caseSensitive = bCaseSensitive ? *bCaseSensitive : false;
	if (bAutoSort())
		SortRows();
}

void UListWindow::RemoveSortColumn(int colIndex)
{
	if (!IsColumnValid(colIndex))
		return;
	columns[colIndex].sortOrder = -1;
	if (bAutoSort())
		SortRows();
}

void UListWindow::ResetSortColumns(std::optional<bool> bSort)
{
	bool sort = bSort ? *bSort : true;
	for (Column& col : columns)
	{
		col.sortOrder = sort ? 0 : -1;
		col.reverse = false;
		col.caseSensitive = false;
	}
	if (bAutoSort())
		SortRows();
}

void UListWindow::Sort()
{
	SortRows();
}

void UListWindow::EnableAutoSort(std::optional<bool> bNewAutoSort)
{
	bool enable = bNewAutoSort ? *bNewAutoSort : true;
	if (bAutoSort() == enable)
		return;
	bAutoSort() = enable;
	if (enable)
		SortRows();
}

bool UListWindow::IsAutoSortEnabled()
{
	return bAutoSort();
}

/////////////////////////////////////////////////////////////////////////////
// Columns

void UListWindow::InitColumn(Column& col)
{
	col = Column();
	col.width = colMargin() * 2.0f + 20.0f;
	col.color = TextColor();
	col.font = normalFont();
}

void UListWindow::SetNumColumns(int newCols)
{
	if (newCols < 0)
		newCols = 0;
	size_t oldCount = columns.size();
	columns.resize((size_t)newCols);
	for (size_t i = oldCount; i < columns.size(); i++)
		InitColumn(columns[i]);
	for (Row* row : rows)
		ExtendRow(row);
	ComputeRowSize();
	AskParentForReconfigure();
}

int UListWindow::GetNumColumns()
{
	return (int)columns.size();
}

void UListWindow::ComputeRowSize()
{
	float size = 0.0f;
	for (const Column& col : columns)
		size = std::max(size, FontHeight(col.font));
	lineSize() = size + rowMargin() * 2.0f;
}

void UListWindow::ResizeColumns(std::optional<bool> bExpandOnly)
{
	if (!bExpandOnly || !*bExpandOnly)
	{
		for (Column& col : columns)
			col.width = colMargin() * 2.0f;
	}
	for (Row* row : rows)
	{
		for (size_t i = 0; i < columns.size(); i++)
			ExpandColumnByField(row, (int)i);
	}
	AskParentForReconfigure();
}

void UListWindow::SetColumnTitle(int colIndex, const std::string& Title)
{
	if (IsColumnValid(colIndex))
		columns[colIndex].title = Title;
}

std::string UListWindow::GetColumnTitle(int colIndex)
{
	return IsColumnValid(colIndex) ? columns[colIndex].title : std::string();
}

void UListWindow::SetColumnWidth(int colIndex, float newWidth)
{
	if (!IsColumnValid(colIndex))
		return;
	columns[colIndex].width = newWidth;
	AskParentForReconfigure();
}

float UListWindow::GetColumnWidth(int colIndex)
{
	return IsColumnValid(colIndex) ? columns[colIndex].width : 0.0f;
}

void UListWindow::SetColumnAlignment(int colIndex, uint8_t newAlign)
{
	if (IsColumnValid(colIndex))
		columns[colIndex].align = (EHAlign)newAlign;
}

uint8_t UListWindow::GetColumnAlignment(int colIndex)
{
	return IsColumnValid(colIndex) ? (uint8_t)columns[colIndex].align : 0;
}

void UListWindow::SetColumnColor(int colIndex, const Color& NewColor)
{
	if (IsColumnValid(colIndex))
		columns[colIndex].color = NewColor;
}

void UListWindow::GetColumnColor(int colIndex, Color& colColor)
{
	if (IsColumnValid(colIndex))
		colColor = columns[colIndex].color;
	else
		colColor = { 255, 255, 255, 255 };
}

void UListWindow::SetColumnFont(int colIndex, UObject* NewFont)
{
	if (!IsColumnValid(colIndex))
		return;
	columns[colIndex].font = UObject::TryCast<UFont>(NewFont);
	for (Row* row : rows)
		ComputeFieldSize(row, colIndex);
	ComputeRowSize();
	if (bAutoExpandColumns())
		ResizeColumns(true);
	else
		AskParentForReconfigure();
}

UObject* UListWindow::GetColumnFont(int colIndex)
{
	return IsColumnValid(colIndex) ? columns[colIndex].font : nullptr;
}

void UListWindow::SetColumnType(int colIndex, uint8_t newType, std::optional<std::string> newFmt)
{
	if (!IsColumnValid(colIndex))
		return;

	Column& col = columns[colIndex];
	col.type = newType;
	if (newFmt)
		col.format = *newFmt;
	else if (newType == 1)
		col.format = "%f";
	else if (newType == 2)
		col.format = "%02h:%02m";
	else
		col.format.clear();

	// The fields get the text for the new type
	for (Row* row : rows)
	{
		std::string text = row->fields[colIndex].text;
		SetFieldByString(row, colIndex, text);
		ComputeFieldSize(row, colIndex);
	}
	if (bAutoExpandColumns())
		ResizeColumns(true);
}

uint8_t UListWindow::GetColumnType(int colIndex)
{
	return IsColumnValid(colIndex) ? columns[colIndex].type : 0;
}

void UListWindow::HideColumn(int colIndex, std::optional<bool> bHide)
{
	if (!IsColumnValid(colIndex))
		return;
	bool hide = bHide ? *bHide : true;
	if (columns[colIndex].hidden != hide)
	{
		columns[colIndex].hidden = hide;
		AskParentForReconfigure();
	}
}

bool UListWindow::IsColumnHidden(int colIndex)
{
	return IsColumnValid(colIndex) && columns[colIndex].hidden;
}

void UListWindow::EnableAutoExpandColumns(std::optional<bool> bAutoExpand)
{
	bool enable = bAutoExpand ? *bAutoExpand : true;
	if (bAutoExpandColumns() == enable)
		return;
	bAutoExpandColumns() = enable;
	if (enable)
		ResizeColumns(true);
}

bool UListWindow::IsAutoExpandColumnsEnabled()
{
	return bAutoExpandColumns();
}

void UListWindow::SetFieldMargins(float newMarginWidth, float newMarginHeight)
{
	colMargin() = newMarginWidth;
	rowMargin() = newMarginHeight;
	ComputeRowSize();
	if (bAutoExpandColumns())
		ResizeColumns(true);
	else
		AskParentForReconfigure();
}

void UListWindow::GetFieldMargins(float& marginWidth, float& marginHeight)
{
	marginWidth = colMargin();
	marginHeight = rowMargin();
}

void UListWindow::SetDelimiter(const std::string& newDelimiter)
{
	Delimiter() = newDelimiter.empty() ? std::string(";") : newDelimiter.substr(0, 1);
}

/////////////////////////////////////////////////////////////////////////////
// Selection and focus

void UListWindow::ChangeSelectRow(int index, bool select, bool& changed)
{
	if (index < 0 || (size_t)index >= rows.size())
		return;
	Row* row = rows[index];
	if (row->selected == select)
		return;
	row->selected = select;
	numSelected() += select ? 1 : -1;
	changed = true;
}

// XListWindow::MoveToRow: the one place that changes selection and focus for the mouse and the keyboard
void UListWindow::MoveToRow(Row* row, bool select, bool clearRows, bool invert, bool spanRows, bool setFocus)
{
	int oldFocus = focusLine();
	bool changed = false;

	if (!bMultiSelect())
	{
		spanRows = false;
		clearRows = true;
	}

	int index = row ? row->index : 0;

	if (select)
	{
		Row* focus = FocusRow();
		Row* anchor = AnchorRow();
		int focusIndex = focus ? focus->index : 0;
		int anchorIndex = anchor ? anchor->index : focusIndex;

		if (clearRows)
		{
			for (int i = 0; i < (int)rows.size(); i++)
				ChangeSelectRow(i, false, changed);
		}
		else if (spanRows)
		{
			// Take the old span away. The new span comes next.
			for (int i = std::min(anchorIndex, focusIndex); i <= std::max(anchorIndex, focusIndex); i++)
				ChangeSelectRow(i, false, changed);
		}

		if (spanRows)
		{
			for (int i = std::min(anchorIndex, index); i <= std::max(anchorIndex, index); i++)
				ChangeSelectRow(i, true, changed);
		}

		if (!invert)
			ChangeSelectRow(index, true, changed);
		else if (index >= 0 && (size_t)index < rows.size())
			ChangeSelectRow(index, !rows[index]->selected, changed);
	}

	if (setFocus && index >= 0 && (size_t)index < rows.size())
		SetFocusRow(rows[index]->id, true, !spanRows);

	if (changed)
		ChangeListSelection();

	if (focusLine() != oldFocus && focusLine() != 0)
		PlayListSound(moveSound(), {}, {});
}

void UListWindow::SelectRow(int rowId, std::optional<bool> bSelect)
{
	bool select = bSelect ? *bSelect : true;
	Row* row = FindRow(rowId);
	if (!row || row->selected == select)
		return;

	bool changed = false;
	if (!bMultiSelect() && select && numSelected() > 0)
	{
		for (int i = 0; i < (int)rows.size(); i++)
			ChangeSelectRow(i, false, changed);
	}
	ChangeSelectRow(row->index, select, changed);
	if (changed)
		ChangeListSelection();
}

void UListWindow::SelectAllRows(std::optional<bool> bSelect)
{
	bool select = bSelect ? *bSelect : true;
	if (!bMultiSelect())
		select = false;

	bool changed = false;
	for (int i = 0; i < (int)rows.size(); i++)
		ChangeSelectRow(i, select, changed);
	if (changed)
		ChangeListSelection();
}

void UListWindow::SelectToRow(int rowId, std::optional<bool> bClearRows, std::optional<bool> bInvert, std::optional<bool> bSpanRows)
{
	if (Row* row = FindRow(rowId))
		MoveToRow(row, true, bClearRows ? *bClearRows : true, bInvert ? *bInvert : false, bSpanRows ? *bSpanRows : true, false);
}

void UListWindow::ToggleRowSelection(int rowId)
{
	SelectRow(rowId, !IsRowSelected(rowId));
}

bool UListWindow::IsRowSelected(int rowId)
{
	Row* row = FindRow(rowId);
	return row && row->selected;
}

int UListWindow::GetSelectedRow()
{
	Row* focus = FocusRow();
	if (focus && focus->selected)
		return focus->id;
	for (Row* row : rows)
	{
		if (row->selected)
			return row->id;
	}
	return 0;
}

int UListWindow::GetNumSelectedRows()
{
	return numSelected();
}

void UListWindow::MoveRow(uint8_t Move, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag)
{
	bool drag = bDrag ? *bDrag : false;

	Row* focus = FocusRow();
	int index = focus ? focus->index : -0xffff;
	switch (Move)
	{
	case 0: index--; break; // MOVELIST_Up
	case 1: index++; break; // MOVELIST_Down
	case 2: index -= GetPageSize(); break; // MOVELIST_PageUp
	case 3: index += GetPageSize(); break; // MOVELIST_PageDown
	case 4: index = 0; break; // MOVELIST_Home
	case 5: index = (int)rows.size() - 1; break; // MOVELIST_End
	}

	index = std::min(index, (int)rows.size() - 1);
	index = std::max(index, 0);

	if ((size_t)index >= rows.size())
		SetFocusRow(0, true, !drag);
	else
		SetRow(rows[index]->id, bSelect, bClearRows, bDrag);
}

void UListWindow::SetRow(int rowId, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag)
{
	bool drag = bDrag ? *bDrag : false;
	Row* row = FindRow(rowId);
	if (row)
		MoveToRow(row, bSelect ? *bSelect : true, bClearRows ? *bClearRows : true, false, drag, true);
	else
		SetFocusRow(0, true, !drag);
}

void UListWindow::SetFocusRow(int rowId, std::optional<bool> bMoveTo, std::optional<bool> bAnchor)
{
	if (rowId != 0 && !FindRow(rowId))
		rowId = 0;
	focusLine() = rowId;
	if (!bAnchor || *bAnchor)
		anchorLine() = rowId;
	if (!bMoveTo || *bMoveTo)
		ShowFocusRow();
}

int UListWindow::GetFocusRow()
{
	return FocusRow() ? focusLine() : 0;
}

void UListWindow::ShowFocusRow()
{
	if (Row* focus = FocusRow())
		AskParentToShowArea(0.0f, (float)focus->index * lineSize(), Width(), lineSize());
}

int UListWindow::GetPageSize()
{
	// The number of full rows that the visible part of the list shows
	float visibleHeight = Height();
	if (UWindow* parent = parentOwner())
		visibleHeight = std::min(visibleHeight, parent->Height());
	if (lineSize() > 0.0f)
	{
		int count = (int)(visibleHeight / lineSize());
		if (count > 0)
			return count;
	}
	return 1;
}

void UListWindow::EnableMultiSelect(std::optional<bool> bEnableMultiSelect)
{
	bool enable = bEnableMultiSelect ? *bEnableMultiSelect : true;
	if (bMultiSelect() == enable)
		return;
	bMultiSelect() = enable;

	// One row at most can stay in the selection
	if (!enable && numSelected() > 1)
	{
		int keep = GetSelectedRow();
		bool changed = false;
		for (int i = 0; i < (int)rows.size(); i++)
		{
			if (rows[i]->id != keep)
				ChangeSelectRow(i, false, changed);
		}
		if (changed)
			ChangeListSelection();
	}
}

bool UListWindow::IsMultiSelectEnabled()
{
	return bMultiSelect();
}

// Tells the parents that the user chose a row (double click or Enter)
void UListWindow::ActivateListRow()
{
	int focusRowId = GetFocusRow();
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ListRowActivated(this, focusRowId))
			break;
	}
}

void UListWindow::ChangeListSelection()
{
	int focusRowId = GetFocusRow();
	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ListSelectionChanged(this, numSelected(), focusRowId))
			break;
	}
}

/////////////////////////////////////////////////////////////////////////////
// Hot keys

void UListWindow::EnableHotKeys(std::optional<bool> bEnable)
{
	bHotKeys() = bEnable ? *bEnable : true;
}

void UListWindow::SetHotKeyColumn(int colIndex)
{
	hotKeyCol() = colIndex;
}

void UListWindow::ClearHotKeyString()
{
	hotKeyString().clear();
	hotKeyTimer() = 0.0f;
}

// Finds the next row whose hot key column starts with the characters that the user typed
int UListWindow::FindRowByKey(const std::string& key)
{
	if (!bHotKeys() || !IsColumnValid(hotKeyCol()) || key.empty() || !IsHotKeyValid(key[0]) || rows.empty())
		return 0;

	std::string& typed = hotKeyString();
	typed += key[0];
	hotKeyTimer() = 1.0f;

	// The same character again and again steps through the rows that start with it
	bool sameChar = true;
	for (char c : typed)
	{
		if (c != typed[0])
			sameChar = false;
	}
	size_t matchLength = sameChar ? 1 : typed.size();

	Row* focus = FocusRow();
	int index = focus ? focus->index : 0;
	if (matchLength > 1)
		index--;

	for (size_t count = 0; count < rows.size(); count++)
	{
		index++;
		if (index >= (int)rows.size())
			index = 0;

		const std::string& text = rows[index]->fields[hotKeyCol()].text;
		bool match = true;
		for (size_t i = 0; i < matchLength; i++)
		{
			char c = i < text.size() ? text[i] : '\0';
			if (LowerChar(c) != LowerChar(typed[i]))
			{
				match = false;
				break;
			}
		}
		if (match)
			return rows[index]->id;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Look and sound

void UListWindow::SetFocusColor(const Color& NewColor)
{
	focusColor() = NewColor;
}

void UListWindow::SetFocusTexture(UObject* NewTexture)
{
	focusTexture() = UObject::TryCast<UTexture>(NewTexture);
}

void UListWindow::SetFocusThickness(float newThickness)
{
	focusThickness() = newThickness;
}

void UListWindow::SetHighlightColor(const Color& NewColor)
{
	highlightColor() = NewColor;
}

void UListWindow::SetHighlightTextColor(const Color& NewColor)
{
	inverseColor() = NewColor;
}

void UListWindow::SetHighlightTexture(UObject* NewTexture)
{
	highlightTexture() = UObject::TryCast<UTexture>(NewTexture);
}

void UListWindow::SetListSounds(std::optional<UObject*> newActivateSound, std::optional<UObject*> newMoveSound)
{
	ActivateSound() = newActivateSound ? UObject::TryCast<USound>(*newActivateSound) : nullptr;
	moveSound() = newMoveSound ? UObject::TryCast<USound>(*newMoveSound) : nullptr;
}

void UListWindow::PlayListSound(UObject* listSound, std::optional<float> Volume, std::optional<float> Pitch)
{
	if (listSound)
		PlaySound(listSound, (Volume && *Volume >= 0.0f) ? Volume : std::optional<float>(), Pitch, {}, {});
}

/////////////////////////////////////////////////////////////////////////////
// Layout and drawing

void UListWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	preferredWidth = 0.0f;
	for (const Column& col : columns)
	{
		if (!col.hidden)
			preferredWidth += col.width;
	}
	preferredHeight = (float)rows.size() * lineSize();
}

void UListWindow::ParentRequestedGranularity(float& hGranularity, float& vGranularity)
{
	hGranularity = 1.0f;
	vGranularity = lineSize();
	UWindow::ParentRequestedGranularity(hGranularity, vGranularity);
}

void UListWindow::DrawWindow(UGC* gc)
{
	float line = lineSize();
	float width = Width();

	// The original keeps the state of the drawing context with PushGC and PopGC
	uint8_t oldStyle = gc->Style();
	uint8_t oldHAlign = gc->HAlign();
	uint8_t oldVAlign = gc->VAlign();
	bool oldWordWrap = gc->bWordWrap();
	bool oldSpecialText = gc->SpecialTextEnabled;
	UFont* oldNormalFont = gc->normalFont();
	UFont* oldBoldFont = gc->boldFont();
	Color oldTextColor = gc->TextColor();
	Color oldTileColor = gc->tileColor();

	if (line > 0.0f && !rows.empty())
	{
		gc->SetStyle(EDrawStyle::Masked);
		gc->EnableWordWrap(false);
		gc->SetAlignments((uint8_t)EHAlign::Left, (uint8_t)EVAlign::Top);
		gc->EnableSpecialText(false);

		// Selected rows
		gc->SetTileColor(highlightColor());
		for (Row* row : rows)
		{
			if (row->selected && highlightTexture())
				gc->DrawPattern(0.0f, (float)row->index * line, width, line, 0.0f, 0.0f, highlightTexture());
		}

		// The text, one column at a time
		float colX = 0.0f;
		for (size_t colIndex = 0; colIndex < columns.size(); colIndex++)
		{
			const Column& col = columns[colIndex];
			if (col.hidden)
				continue;

			if (col.font)
			{
				gc->SetFonts(col.font, col.font);
				float clipX = colX + colMargin();
				float clipWidth = col.width - colMargin() * 2.0f;

				for (Row* row : rows)
				{
					const Field& field = row->fields[colIndex];
					if (field.text.empty())
						continue;

					float rowY = (float)row->index * line;
					gc->SetTextColor(row->selected ? inverseColor() : col.color);

					float textX;
					if (col.align == EHAlign::Left)
						textX = colX + colMargin();
					else if (col.align == EHAlign::Right)
						textX = colX - colMargin() + (col.width - field.width);
					else
						textX = colX + (col.width - field.width) * 0.5f;
					float textY = rowY - rowMargin() + (line - field.height) + 1.0f;

					gc->PushClip(gc->ScaleRect(Rectf::xywh(gc->offsetX + clipX, gc->offsetY + rowY + rowMargin(), std::max(clipWidth, 0.0f), std::max(line - rowMargin() * 2.0f, 0.0f))));
					gc->DrawText(textX, textY, field.width, field.height, field.text);
					gc->PopClip();
				}
			}

			colX += col.width;
		}

		// The focus box
		Row* focus = FocusRow();
		if (focus && IsFocusWindow() && focusTexture())
		{
			gc->SetTileColor(focusColor());
			gc->DrawBox(0.0f, (float)focus->index * line, width, line, 0.0f, 0.0f, focusThickness(), focusTexture());
		}
	}

	gc->SetStyle((EDrawStyle)oldStyle);
	gc->SetAlignments(oldHAlign, oldVAlign);
	gc->EnableWordWrap(oldWordWrap);
	gc->EnableSpecialText(oldSpecialText);
	gc->SetFonts(oldNormalFont, oldBoldFont);
	gc->SetTextColor(oldTextColor);
	gc->SetTileColor(oldTileColor);

	UWindow::DrawWindow(gc);
}

/////////////////////////////////////////////////////////////////////////////
// Events

void UListWindow::VisibilityChanged(bool bNewVisibility)
{
	UWindow::VisibilityChanged(bNewVisibility);

	// A list that comes into view puts the focus on its first row
	if (bNewVisibility && !FocusRow() && !rows.empty())
		SetRow(rows[0]->id, true, true, false);
}

int UListWindow::RowIndexAt(float pointY) const
{
	float line = const_cast<UListWindow*>(this)->lineSize();
	if (line <= 0.0f || rows.empty())
		return -1;
	int index = (int)std::floor(pointY / line);
	index = std::min(index, (int)rows.size() - 1);
	return std::max(index, 0);
}

bool UListWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	UWindow::MouseButtonPressed(pointX, pointY, button, numClicks);

	if (button != IK_LeftMouse)
		return false;

	SetFocusWindow(this);

	int index = RowIndexAt(pointY);
	if (index >= 0)
	{
		bool shift = IsKeyDown(IK_Shift);
		bool ctrl = IsKeyDown(IK_Ctrl);
		MoveToRow(rows[index], true, !ctrl, ctrl, shift, true);
		lastIndex() = index;
		bDragging() = true;
		remainingDelay() = 0.1f;
	}
	return true;
}

void UListWindow::MouseMoved(float newX, float newY)
{
	UWindow::MouseMoved(newX, newY);

	if (!bDragging())
		return;

	// A drag with the button down moves the focus and makes the selection longer
	int index = RowIndexAt(newY);
	if (index >= 0 && index != lastIndex())
	{
		lastIndex() = index;
		MoveToRow(rows[index], true, false, false, true, true);
	}
}

bool UListWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	UWindow::MouseButtonReleased(pointX, pointY, button, numClicks);

	if (button != IK_LeftMouse)
		return false;

	bDragging() = false;

	// The second click of a double click chooses the row
	if (numClicks > 1 && lineSize() > 0.0f && !rows.empty())
	{
		int index = (int)std::floor(pointY / lineSize());
		if (index >= 0 && (size_t)index < rows.size())
		{
			SetFocusRow(rows[index]->id, true, true);
			PlayListSound(ActivateSound(), {}, {});
			ActivateListRow();
		}
	}
	return true;
}

bool UListWindow::KeyPressed(std::string key)
{
	bool handled = UWindow::KeyPressed(key);

	if (!key.empty() && IsHotKeyValid(key[0]) && bHotKeys())
	{
		int rowId = FindRowByKey(key);
		if (rowId != 0)
			SetRow(rowId, true, true, false);
		handled = true;
	}
	return handled;
}

bool UListWindow::VirtualKeyPressed(EInputKey key, bool bRepeat)
{
	bool handled = UWindow::VirtualKeyPressed(key, bRepeat);

	if (key == IK_Left || key == IK_Right || key == IK_Up || key == IK_Down || key == IK_Escape)
		ClearHotKeyString();

	if (key == IK_Enter)
	{
		PlayListSound(ActivateSound(), {}, {});
		ActivateListRow();
		handled = true;
	}
	return handled;
}

void UListWindow::Tick(float timeElapsed)
{
	if (hotKeyTimer() > 0.0f)
	{
		hotKeyTimer() -= timeElapsed;
		if (hotKeyTimer() <= 0.0f)
			ClearHotKeyString();
	}

	UWindow::Tick(timeElapsed);
}
