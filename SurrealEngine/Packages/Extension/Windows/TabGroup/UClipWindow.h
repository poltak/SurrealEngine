#pragma once

#include "UTabGroupWindow.h"

class UClipWindow : public UTabGroupWindow
{
public:
	using UTabGroupWindow::UTabGroupWindow;

	void InitWindow() override;
	void ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight) override;
	void ChildRequestedVisibilityChange(UWindow* childWin, bool bNewVisibility) override;
	void ChildRequestedShowArea(UWindow* child, float showX, float showY, float showWidth, float showHeight) override;
	void ConfigurationChanged() override;

	UWindow* GetChildWindow();
	void QueryClipPreferredSize(bool bWidthSpecified, float width, float& outWidth, bool& outChildWider, bool bHeightSpecified, float height, float& outHeight, bool& outChildHigher);

	void EnableSnapToUnits(std::optional<bool> bNewSnapToUnits);
	void ForceChildSize(std::optional<bool> bNewForceChildWidth, std::optional<bool> bNewForceChildHeight);
	UObject* GetChild();
	void GetChildPosition(int& pNewX, int& pNewY);
	void GetUnitSize(int& pAreaHSize, int& pAreaVSize, int& pChildHSize, int& childVSize);
	void ResetUnitHeight();
	void ResetUnitSize();
	void ResetUnitWidth();
	void SetChildPosition(int newX, int newY);
	void SetUnitHeight(int vUnits);
	void SetUnitSize(int hUnits, int vUnits);
	void SetUnitWidth(int hUnits);

	int& areaHSize() { return Value<int>(PropOffsets_ClipWindow.areaHSize); }
	int& areaVSize() { return Value<int>(PropOffsets_ClipWindow.areaVSize); }
	BitfieldBool bFillWindow() { return BoolValue(PropOffsets_ClipWindow.bFillWindow); }
	BitfieldBool bForceChildHeight() { return BoolValue(PropOffsets_ClipWindow.bForceChildHeight); }
	BitfieldBool bForceChildWidth() { return BoolValue(PropOffsets_ClipWindow.bForceChildWidth); }
	BitfieldBool bSnapToUnits() { return BoolValue(PropOffsets_ClipWindow.bSnapToUnits); }
	int& childH() { return Value<int>(PropOffsets_ClipWindow.childH); }
	int& childHSize() { return Value<int>(PropOffsets_ClipWindow.childHSize); }
	int& childV() { return Value<int>(PropOffsets_ClipWindow.childV); }
	int& childVSize() { return Value<int>(PropOffsets_ClipWindow.childVSize); }
	float& hMult() { return Value<float>(PropOffsets_ClipWindow.hMult); }
	int& prefHUnits() { return Value<int>(PropOffsets_ClipWindow.prefHUnits); }
	int& prefVUnits() { return Value<int>(PropOffsets_ClipWindow.prefVUnits); }
	float& vMult() { return Value<float>(PropOffsets_ClipWindow.vMult); }

private:
	// The position in units and the size in pixels that the child gets
	struct ChildConfig
	{
		int hPos = 0;
		int vPos = 0;
		float width = 0.0f;
		float height = 0.0f;
	};

	void GetChildUnits(UWindow* child, float& hUnit, float& vUnit);
	void ClampChildPosition(float& x, float& y, float childWidth, float childHeight);
	ChildConfig GetChildPreferredSize(UWindow* child, bool bWidthSpecified, float width, bool bHeightSpecified, float height);
	void GetClipPreferredSize(UWindow* child, bool bWidthSpecified, float& width, bool bHeightSpecified, float& height, ChildConfig* childConfig);
	void ReconfigureChild(UWindow* child, int hPos, int vPos, float width, float height);
	static int ShowAreaMove(int start, int span, int area);
};
