
#include "Precomp.h"
#include "UScrollAreaWindow.h"
#include "UScaleManagerWindow.h"
#include "UScaleWindow.h"
#include "TabGroup/UClipWindow.h"
#include "Text/UButtonWindow.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"

// A scroll area has a clip window with the content, and a scale manager with two buttons and a scale for each
// direction. The clip window tells the scales how large the content is and where it is. A scale tells the clip
// window where the thumb is. The functions of this file follow XScrollAreaWindow in Extension.dll.

void UScrollAreaWindow::InitWindow()
{
	marginWidth() = 3.0f;
	marginHeight() = 3.0f;
	scrollbarDistance() = 3.0f;
	bHideScrollbars() = true;
	bHLastShow() = false;
	bVLastShow() = false;

	UClass* managerClass = engine->packages->FindClass("Extension.ScaleManagerWindow");
	UClass* clipClass = engine->packages->FindClass("Extension.ClipWindow");
	UClass* buttonClass = engine->packages->FindClass("Extension.ButtonWindow");
	UClass* scaleClass = engine->packages->FindClass("Extension.ScaleWindow");

	hScaleMgr() = UObject::Cast<UScaleManagerWindow>(NewChild(managerClass, false));
	vScaleMgr() = UObject::Cast<UScaleManagerWindow>(NewChild(managerClass, false));
	ClipWindow() = UObject::Cast<UClipWindow>(NewChild(clipClass, true));

	LeftButton() = UObject::Cast<UButtonWindow>(hScaleMgr()->NewChild(buttonClass, true));
	hScale() = UObject::Cast<UScaleWindow>(hScaleMgr()->NewChild(scaleClass, true));
	RightButton() = UObject::Cast<UButtonWindow>(hScaleMgr()->NewChild(buttonClass, true));

	UpButton() = UObject::Cast<UButtonWindow>(vScaleMgr()->NewChild(buttonClass, true));
	vScale() = UObject::Cast<UScaleWindow>(vScaleMgr()->NewChild(scaleClass, true));
	DownButton() = UObject::Cast<UButtonWindow>(vScaleMgr()->NewChild(buttonClass, true));

	hScaleMgr()->SetManagerOrientation((uint8_t)EOrientation::Horizontal);
	hScaleMgr()->SetScaleButtons(LeftButton(), RightButton());
	hScaleMgr()->SetScale(hScale());
	hScaleMgr()->StretchScaleField(true);

	vScaleMgr()->SetManagerOrientation((uint8_t)EOrientation::Vertical);
	vScaleMgr()->SetScaleButtons(UpButton(), DownButton());
	vScaleMgr()->SetScale(vScale());
	vScaleMgr()->StretchScaleField(true);

	hScale()->SetScaleOrientation((uint8_t)EOrientation::Horizontal);
	vScale()->SetScaleOrientation((uint8_t)EOrientation::Vertical);
	for (UScaleWindow* scale : { hScale(), vScale() })
	{
		scale->SetSelectability(false);
		scale->SetNumTicks(1);
		scale->SetThumbSpan(1);
		scale->EnableStretchedScale(true);
	}

	for (UButtonWindow* button : { LeftButton(), RightButton(), UpButton(), DownButton() })
	{
		button->SetSelectability(false);
		button->EnableAutoRepeat(true, 0.5f, 0.1f);
	}

	EnableScrolling(false, true);
	SetSelectability(false);

	UWindow::InitWindow();
}

void UScrollAreaWindow::EnableScrolling(std::optional<bool> bHScrolling, std::optional<bool> bVScrolling)
{
	bool horizontal = !bHScrolling || *bHScrolling;
	bool vertical = !bVScrolling || *bVScrolling;

	if (hScaleMgr())
		hScaleMgr()->SetVisibility(horizontal);
	if (vScaleMgr())
		vScaleMgr()->SetVisibility(vertical);

	// In a direction with no scrolling, the content gets the size of the clip window
	if (ClipWindow())
		ClipWindow()->ForceChildSize(!horizontal, !vertical);
}

void UScrollAreaWindow::AutoHideScrollbars(std::optional<bool> bHide)
{
	bool hide = !bHide || *bHide;
	if (bHideScrollbars() != hide)
	{
		bHideScrollbars() = hide;
		AskParentForReconfigure();
	}
}

void UScrollAreaWindow::SetAreaMargins(float newMarginWidth, float newMarginHeight)
{
	if (marginWidth() != newMarginWidth || marginHeight() != newMarginHeight)
	{
		marginWidth() = newMarginWidth;
		marginHeight() = newMarginHeight;
		AskParentForReconfigure();
	}
}

void UScrollAreaWindow::SetScrollbarDistance(float newDistance)
{
	if (scrollbarDistance() != newDistance)
	{
		scrollbarDistance() = newDistance;
		AskParentForReconfigure();
	}
}

// Finds the place and the size of the clip window and of the two scroll bars for a size of the scroll area.
// With automatic hiding, a scroll bar shows only when the content is larger than the clip window.
UScrollAreaWindow::Layout UScrollAreaWindow::ComputeChildSizes(bool bWidthSpecified, float width, bool bHeightSpecified, float height)
{
	Layout layout;

	UWindow* hManager = (hScaleMgr() && hScaleMgr()->bIsVisible()) ? hScaleMgr() : nullptr;
	UWindow* vManager = (vScaleMgr() && vScaleMgr()->bIsVisible()) ? vScaleMgr() : nullptr;
	UClipWindow* clip = (ClipWindow() && ClipWindow()->bIsVisible()) ? ClipWindow() : nullptr;
	if (!clip)
	{
		layout.totalWidth = 10.0f;
		layout.totalHeight = 10.0f;
		return layout;
	}

	// The thickness of each bar, and the space that it takes with its distance to the clip window
	float hBarHeight = 0.0f, hBarSpace = 0.0f;
	float vBarWidth = 0.0f, vBarSpace = 0.0f;
	bool showH = false, showV = false;
	bool autoH = false, autoV = false;
	if (hManager)
	{
		float unused = 0.0f;
		hManager->QueryPreferredSize(unused, hBarHeight);
		hBarSpace = hBarHeight + scrollbarDistance();
		showH = true;
	}
	if (vManager)
	{
		float unused = 0.0f;
		vManager->QueryPreferredSize(vBarWidth, unused);
		vBarSpace = vBarWidth + scrollbarDistance();
		showV = true;
	}
	if (bHideScrollbars())
	{
		// Start with the result of the last time
		if (hManager)
		{
			showH = bHLastShow();
			autoH = true;
		}
		if (vManager)
		{
			showV = bVLastShow();
			autoV = true;
		}
	}

	float availableWidth = bWidthSpecified ? width - marginWidth() * 2.0f : 0.0f;
	float availableHeight = bHeightSpecified ? height - marginHeight() * 2.0f : 0.0f;

	// A bar that shows makes the clip window smaller, and then the other bar can become necessary.
	// Try until the result does not change. After the last try, each automatic bar shows.
	float clipWidth = 0.0f, clipHeight = 0.0f;
	int maxTries = 1;
	if (autoH)
		maxTries = 2;
	if (autoV)
		maxTries *= 2;
	for (int tries = 1; ; tries++)
	{
		if (tries > maxTries)
		{
			if (autoH)
				showH = true;
			if (autoV)
				showV = true;
		}

		float hSpace = (showH && bHeightSpecified) ? hBarSpace : 0.0f;
		float vSpace = (showV && bWidthSpecified) ? vBarSpace : 0.0f;
		bool childWider = false, childHigher = false;
		clip->QueryClipPreferredSize(bWidthSpecified, availableWidth - vSpace, clipWidth, childWider, bHeightSpecified, availableHeight - hSpace, clipHeight, childHigher);

		if (tries > maxTries)
			break;

		if (autoH && childWider && !showH)
			showH = true;
		else if (autoV && childHigher && !showV)
			showV = true;
		else if (autoH && !childWider && showH)
			showH = false;
		else if (autoV && !childHigher && showV)
			showV = false;
		else
			break;
	}

	bHLastShow() = showH;
	bVLastShow() = showV;

	layout.clipX = marginWidth();
	layout.clipY = marginHeight();
	layout.clipWidth = clipWidth;
	layout.clipHeight = clipHeight;

	if (hManager)
	{
		layout.hX = layout.clipX;
		layout.hY = scrollbarDistance() + layout.clipHeight + layout.clipY;
		layout.hWidth = layout.clipWidth;
		layout.hHeight = showH ? hBarHeight : 0.0f;
	}
	if (vManager)
	{
		layout.vX = scrollbarDistance() + layout.clipWidth + layout.clipX;
		layout.vY = layout.clipY;
		layout.vWidth = showV ? vBarWidth : 0.0f;
		layout.vHeight = layout.clipHeight;
	}

	layout.totalWidth = marginWidth() * 2.0f + layout.clipWidth;
	if (showV)
		layout.totalWidth += vBarSpace;
	layout.totalHeight = marginHeight() * 2.0f + layout.clipHeight;
	if (showH)
		layout.totalHeight += hBarSpace;
	return layout;
}

void UScrollAreaWindow::ConfigurationChanged()
{
	Layout layout = ComputeChildSizes(true, Width(), true, Height());

	if (hScaleMgr() && hScaleMgr()->bIsVisible())
		hScaleMgr()->ConfigureChild(layout.hX, layout.hY, layout.hWidth, layout.hHeight);
	if (vScaleMgr() && vScaleMgr()->bIsVisible())
		vScaleMgr()->ConfigureChild(layout.vX, layout.vY, layout.vWidth, layout.vHeight);
	if (ClipWindow() && ClipWindow()->bIsVisible())
		ClipWindow()->ConfigureChild(layout.clipX, layout.clipY, layout.clipWidth, layout.clipHeight);
}

void UScrollAreaWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	Layout layout = ComputeChildSizes(bWidthSpecified, preferredWidth, bHeightSpecified, preferredHeight);
	preferredWidth = layout.totalWidth;
	preferredHeight = layout.totalHeight;
}

void UScrollAreaWindow::ChildRequestedVisibilityChange(UWindow* childWin, bool bNewVisibility)
{
	childWin->SetChildVisibility(bNewVisibility);
	AskParentForReconfigure();
}

void UScrollAreaWindow::DescendantRemoved(UWindow* descendant)
{
	UWindow::DescendantRemoved(descendant);

	if (descendant == hScaleMgr())
		hScaleMgr() = nullptr;
	if (descendant == vScaleMgr())
		vScaleMgr() = nullptr;
	if (descendant == hScale())
		hScale() = nullptr;
	if (descendant == vScale())
		vScale() = nullptr;
	if (descendant == LeftButton())
		LeftButton() = nullptr;
	if (descendant == RightButton())
		RightButton() = nullptr;
	if (descendant == UpButton())
		UpButton() = nullptr;
	if (descendant == DownButton())
		DownButton() = nullptr;
	if (descendant == ClipWindow())
		ClipWindow() = nullptr;
}

// The player moved the thumb of a scroll bar: move the content
bool UScrollAreaWindow::ScaleRangeChanged(UWindow* scale, int fromTick, int toTick, float fromValue, float toValue, bool bFinal)
{
	bool handled = UWindow::ScaleRangeChanged(scale, fromTick, toTick, fromValue, toValue, bFinal);
	if (!ClipWindow())
		return handled;

	int column = 0, row = 0;
	ClipWindow()->GetChildPosition(column, row);
	if (scale == hScale())
	{
		column = fromTick;
		handled = true;
	}
	else if (scale == vScale())
	{
		row = fromTick;
		handled = true;
	}
	ClipWindow()->SetChildPosition(column, row);
	return handled;
}

// The size of the content or of the clip window changed: set the length of the scales and of the thumbs
bool UScrollAreaWindow::ClipAttributesChanged(UWindow* clip, int newClipWidth, int newClipHeight, int newChildWidth, int newChildHeight)
{
	bool handled = UWindow::ClipAttributesChanged(clip, newClipWidth, newClipHeight, newChildWidth, newChildHeight);
	if (clip == ClipWindow())
	{
		newChildWidth = std::max(newChildWidth, 1);
		newChildHeight = std::max(newChildHeight, 1);
		if (hScale())
			hScale()->SetRanges(newClipWidth, newChildWidth);
		if (vScale())
			vScale()->SetRanges(newClipHeight, newChildHeight);
		handled = true;
	}
	return handled;
}

// The content moved: move the thumbs
bool UScrollAreaWindow::ClipPositionChanged(UWindow* clip, int newCol, int newRow)
{
	bool handled = UWindow::ClipPositionChanged(clip, newCol, newRow);
	if (clip == ClipWindow())
	{
		if (hScale())
			hScale()->SetTickPosition(newCol);
		if (vScale())
			vScale()->SetTickPosition(newRow);
		handled = true;
	}
	return handled;
}

// The mouse wheel moves the content up and down when the vertical scroll bar is in view
bool UScrollAreaWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UWindow::MouseButtonPressed(pointX, pointY, button, numClicks);
	if (button == IK_MouseWheelUp || button == IK_MouseWheelDown)
	{
		if (vScale() && vScale()->IsVisible({}))
			vScale()->MoveThumb((uint8_t)(button == IK_MouseWheelUp ? EMoveThumb::StepUp : EMoveThumb::StepDown));
		handled = true;
	}
	return handled;
}
