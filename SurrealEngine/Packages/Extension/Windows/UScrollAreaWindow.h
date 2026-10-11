#pragma once

#include "UWindow.h"

class UClipWindow;
class UButtonWindow;

class UScrollAreaWindow : public UWindow
{
public:
	using UWindow::UWindow;

	void InitWindow() override;
	void ConfigurationChanged() override;
	void ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight) override;
	void ChildRequestedVisibilityChange(UWindow* childWin, bool bNewVisibility) override;
	void DescendantRemoved(UWindow* descendant) override;
	bool ScaleRangeChanged(UWindow* scale, int fromTick, int toTick, float fromValue, float toValue, bool bFinal) override;
	bool ClipAttributesChanged(UWindow* clip, int newClipWidth, int newClipHeight, int newChildWidth, int newChildHeight) override;
	bool ClipPositionChanged(UWindow* clip, int newCol, int newRow) override;
	bool MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks) override;

	void AutoHideScrollbars(std::optional<bool> bHide);
	void EnableScrolling(std::optional<bool> bHScrolling, std::optional<bool> bVScrolling);
	void SetAreaMargins(float newMarginWidth, float newMarginHeight);
	void SetScrollbarDistance(float newDistance);

	UClipWindow*& ClipWindow() { return Value<UClipWindow*>(PropOffsets_ScrollAreaWindow.ClipWindow); }
	UButtonWindow*& DownButton() { return Value<UButtonWindow*>(PropOffsets_ScrollAreaWindow.DownButton); }
	UButtonWindow*& LeftButton() { return Value<UButtonWindow*>(PropOffsets_ScrollAreaWindow.LeftButton); }
	UButtonWindow*& RightButton() { return Value<UButtonWindow*>(PropOffsets_ScrollAreaWindow.RightButton); }
	UButtonWindow*& UpButton() { return Value<UButtonWindow*>(PropOffsets_ScrollAreaWindow.UpButton); }
	BitfieldBool bHLastShow() { return BoolValue(PropOffsets_ScrollAreaWindow.bHLastShow); }
	BitfieldBool bHideScrollbars() { return BoolValue(PropOffsets_ScrollAreaWindow.bHideScrollbars); }
	BitfieldBool bVLastShow() { return BoolValue(PropOffsets_ScrollAreaWindow.bVLastShow); }
	UScaleWindow*& hScale() { return Value<UScaleWindow*>(PropOffsets_ScrollAreaWindow.hScale); }
	UScaleManagerWindow*& hScaleMgr() { return Value<UScaleManagerWindow*>(PropOffsets_ScrollAreaWindow.hScaleMgr); }
	float& marginHeight() { return Value<float>(PropOffsets_ScrollAreaWindow.marginHeight); }
	float& marginWidth() { return Value<float>(PropOffsets_ScrollAreaWindow.marginWidth); }
	float& scrollbarDistance() { return Value<float>(PropOffsets_ScrollAreaWindow.scrollbarDistance); }
	UScaleWindow*& vScale() { return Value<UScaleWindow*>(PropOffsets_ScrollAreaWindow.vScale); }
	UScaleManagerWindow*& vScaleMgr() { return Value<UScaleManagerWindow*>(PropOffsets_ScrollAreaWindow.vScaleMgr); }

private:
	// The place and size of the clip window and of the two scroll bars, and the size that the scroll area wants
	struct Layout
	{
		float clipX = 0.0f, clipY = 0.0f, clipWidth = 0.0f, clipHeight = 0.0f;
		float hX = 0.0f, hY = 0.0f, hWidth = 0.0f, hHeight = 0.0f;
		float vX = 0.0f, vY = 0.0f, vWidth = 0.0f, vHeight = 0.0f;
		float totalWidth = 0.0f, totalHeight = 0.0f;
	};

	Layout ComputeChildSizes(bool bWidthSpecified, float width, bool bHeightSpecified, float height);
};
