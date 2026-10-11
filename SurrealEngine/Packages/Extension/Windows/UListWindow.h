#pragma once

#include "UWindow.h"
#include <map>
#include <memory>

class UListWindow : public UWindow
{
public:
	using UWindow::UWindow;

	int AddRow(const std::string& rowStr, std::optional<int> clientData);
	void AddSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive);
	void DeleteAllRows();
	void DeleteRow(int rowId);
	void EnableAutoExpandColumns(std::optional<bool> bAutoExpand);
	void EnableAutoSort(std::optional<bool> bAutoSort);
	void EnableHotKeys(std::optional<bool> bEnable);
	void EnableMultiSelect(std::optional<bool> bEnableMultiSelect);
	uint8_t GetColumnAlignment(int colIndex);
	void GetColumnColor(int colIndex, Color& colColor);
	UObject* GetColumnFont(int colIndex);
	std::string GetColumnTitle(int colIndex);
	uint8_t GetColumnType(int colIndex);
	float GetColumnWidth(int colIndex);
	std::string GetField(int rowId, int colIndex);
	void GetFieldMargins(float& marginWidth, float& marginHeight);
	float GetFieldValue(int rowId, int colIndex);
	int GetFocusRow();
	int GetNumColumns();
	int GetNumRows();
	int GetNumSelectedRows();
	int GetPageSize();
	int GetRowClientInt(int rowId);
	UObject* GetRowClientObject(int rowId);
	int GetSelectedRow();
	void HideColumn(int colIndex, std::optional<bool> bHide);
	int IndexToRowId(int index);
	bool IsAutoExpandColumnsEnabled();
	bool IsAutoSortEnabled();
	bool IsColumnHidden(int colIndex);
	bool IsMultiSelectEnabled();
	bool IsRowSelected(int rowId);
	void ModifyRow(int rowId, const std::string& rowStr);
	void MoveRow(uint8_t Move, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag);
	void PlayListSound(UObject* listSound, std::optional<float> Volume, std::optional<float> Pitch);
	void RemoveSortColumn(int colIndex);
	void ResetSortColumns(std::optional<bool> bSort);
	void ResizeColumns(std::optional<bool> bExpandOnly);
	int RowIdToIndex(int rowId);
	void SelectAllRows(std::optional<bool> bSelect);
	void SelectRow(int rowId, std::optional<bool> bSelect);
	void SelectToRow(int rowId, std::optional<bool> bClearRows, std::optional<bool> bInvert, std::optional<bool> bSpanRows);
	void SetColumnAlignment(int colIndex, uint8_t newAlign);
	void SetColumnColor(int colIndex, const Color& NewColor);
	void SetColumnFont(int colIndex, UObject* NewFont);
	void SetColumnTitle(int colIndex, const std::string& Title);
	void SetColumnType(int colIndex, uint8_t newType, std::optional<std::string> newFmt);
	void SetColumnWidth(int colIndex, float newWidth);
	void SetDelimiter(const std::string& newDelimiter);
	void SetField(int rowId, int colIndex, const std::string& fieldStr);
	void SetFieldMargins(float newMarginWidth, float newMarginHeight);
	void SetFieldValue(int rowId, int colIndex, float NewValue);
	void SetFocusColor(const Color& NewColor);
	void SetFocusRow(int rowId, std::optional<bool> bMoveTo, std::optional<bool> bAnchor);
	void SetFocusTexture(UObject* NewTexture);
	void SetFocusThickness(float newThickness);
	void SetHighlightColor(const Color& NewColor);
	void SetHighlightTextColor(const Color& NewColor);
	void SetHighlightTexture(UObject* NewTexture);
	void SetHotKeyColumn(int colIndex);
	void SetListSounds(std::optional<UObject*> ActivateSound, std::optional<UObject*> moveSound);
	void SetNumColumns(int newCols);
	void SetRow(int rowId, std::optional<bool> bSelect, std::optional<bool> bClearRows, std::optional<bool> bDrag);
	void SetRowClientInt(int rowId, int clientInt);
	void SetRowClientObject(int rowId, UObject* clientObj);
	void SetSortColumn(int colIndex, std::optional<bool> bReverse, std::optional<bool> bCaseSensitive);
	void ShowFocusRow();
	void Sort();
	void ToggleRowSelection(int rowId);

	void InitWindow() override;
	void DrawWindow(UGC* gc) override;
	void ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight) override;
	void ParentRequestedGranularity(float& hGranularity, float& vGranularity) override;
	void VisibilityChanged(bool bNewVisibility) override;
	void MouseMoved(float newX, float newY) override;
	bool MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks) override;
	bool MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks) override;
	bool KeyPressed(std::string key) override;
	bool VirtualKeyPressed(EInputKey key, bool bRepeat) override;
	void Tick(float timeElapsed) override;

private:
	// Follows XListColData of Extension.dll
	struct Column
	{
		std::string title;
		int sortOrder = 0; // -1: not a sort key. The highest value is the first sort key.
		bool reverse = false;
		bool caseSensitive = false;
		bool hidden = false;
		float width = 0.0f;
		EHAlign align = EHAlign::Left;
		Color color = { 255, 255, 255, 255 };
		UFont* font = nullptr;
		uint8_t type = 0; // EColumnType: string, float, time
		std::string format;
	};

	struct Field
	{
		std::string text;
		float value = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	// Follows XListRowData of Extension.dll. The original uses the address of the row as the row id.
	struct Row
	{
		int id = 0;
		int index = -1;
		bool selected = false;
		int clientInt = 0;
		UObject* clientObj = nullptr;
		std::vector<Field> fields;
	};

	Row* FindRow(int rowId);
	bool IsColumnValid(int colIndex) const { return colIndex >= 0 && (size_t)colIndex < columns.size(); }
	void InitColumn(Column& col);
	void ComputeRowSize();
	void ComputeFieldSize(Row* row, int colIndex);
	bool ExpandColumnByField(Row* row, int colIndex);
	void ExtendRow(Row* row);
	bool FillRow(Row* row, const std::string& rowStr);
	void SetFieldByString(Row* row, int colIndex, const std::string& text);
	void SetFieldByValue(Row* row, int colIndex, float value);
	std::vector<const Column*> SortedColumns() const;
	int CompareRows(const Row* a, const Row* b, const std::vector<const Column*>& sortCols) const;
	bool FindRowData(const Row* row, int& position) const;
	void SortRows();
	void CheckPosition(Row* row);
	void InsertRow(Row* row);
	void RemoveRow(Row* row);
	void RecomputeIndices(size_t first = 0);
	void ChangeSelectRow(int index, bool select, bool& changed);
	void MoveToRow(Row* row, bool select, bool clearRows, bool invert, bool spanRows, bool setFocus);
	void ActivateListRow();
	void ChangeListSelection();
	int FindRowByKey(const std::string& key);
	void ClearHotKeyString();
	int RowIndexAt(float pointY) const;
	Row* FocusRow() { return FindRow(focusLine()); }
	Row* AnchorRow() { return FindRow(anchorLine()); }

	std::vector<Column> columns;
	std::vector<Row*> rows; // in the order of the display
	std::map<int, std::unique_ptr<Row>> rowsById;
	int nextRowId = 1;

public:
	USound*& ActivateSound() { return Value<USound*>(PropOffsets_ListWindow.ActivateSound); }
	std::string& Delimiter() { return Value<std::string>(PropOffsets_ListWindow.Delimiter); }
	int& anchorLine() { return Value<int>(PropOffsets_ListWindow.anchorLine); }
	BitfieldBool bAutoExpandColumns() { return BoolValue(PropOffsets_ListWindow.bAutoExpandColumns); }
	BitfieldBool bAutoSort() { return BoolValue(PropOffsets_ListWindow.bAutoSort); }
	BitfieldBool bDragging() { return BoolValue(PropOffsets_ListWindow.bDragging); }
	BitfieldBool bHotKeys() { return BoolValue(PropOffsets_ListWindow.bHotKeys); }
	BitfieldBool bMultiSelect() { return BoolValue(PropOffsets_ListWindow.bMultiSelect); }
	float& colMargin() { return Value<float>(PropOffsets_ListWindow.colMargin); }
	//DynamicArray& cols() { return Value<DynamicArray>(PropOffsets_ListWindow.cols); }
	Color& focusColor() { return Value<Color>(PropOffsets_ListWindow.focusColor); }
	int& focusLine() { return Value<int>(PropOffsets_ListWindow.focusLine); }
	UTexture*& focusTexture() { return Value<UTexture*>(PropOffsets_ListWindow.focusTexture); }
	float& focusThickness() { return Value<float>(PropOffsets_ListWindow.focusThickness); }
	Color& highlightColor() { return Value<Color>(PropOffsets_ListWindow.highlightColor); }
	UTexture*& highlightTexture() { return Value<UTexture*>(PropOffsets_ListWindow.highlightTexture); }
	int& hotKeyCol() { return Value<int>(PropOffsets_ListWindow.hotKeyCol); }
	std::string& hotKeyString() { return Value<std::string>(PropOffsets_ListWindow.hotKeyString); }
	float& hotKeyTimer() { return Value<float>(PropOffsets_ListWindow.hotKeyTimer); }
	Color& inverseColor() { return Value<Color>(PropOffsets_ListWindow.inverseColor); }
	int& lastIndex() { return Value<int>(PropOffsets_ListWindow.lastIndex); }
	float& lineSize() { return Value<float>(PropOffsets_ListWindow.lineSize); }
	USound*& moveSound() { return Value<USound*>(PropOffsets_ListWindow.moveSound); }
	int& numSelected() { return Value<int>(PropOffsets_ListWindow.numSelected); }
	float& remainingDelay() { return Value<float>(PropOffsets_ListWindow.remainingDelay); }
	float& rowMargin() { return Value<float>(PropOffsets_ListWindow.rowMargin); }
	//DynamicArray& rows() { return Value<DynamicArray>(PropOffsets_ListWindow.rows); }
};
