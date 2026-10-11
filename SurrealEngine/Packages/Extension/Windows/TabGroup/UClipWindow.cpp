
#include "Precomp.h"
#include "UClipWindow.h"
#include "Utils/Logger.h"

// A clip window shows a part of one child window, which can be larger than the clip window. The position of the child
// is in units. A list gives its row height as the unit, and a text window its line height.
// The functions of this file follow XClipWindow in Extension.dll.

void UClipWindow::InitWindow()
{
	prefHUnits() = -1;
	prefVUnits() = -1;
	hMult() = 1.0f;
	vMult() = 1.0f;
	childH() = 0;
	childV() = 0;
	bForceChildWidth() = false;
	bForceChildHeight() = false;
	bSnapToUnits() = true;
	bFillWindow() = true;
	areaHSize() = 0;
	areaVSize() = 0;
	childHSize() = 0;
	childVSize() = 0;

	UTabGroupWindow::InitWindow();
}

// The child of the clip window is its top window that is visible
UWindow* UClipWindow::GetChildWindow()
{
	UWindow* child = lastChild();
	while (child && !child->bIsVisible())
		child = child->prevSibling();
	return child;
}

UObject* UClipWindow::GetChild()
{
	return GetChildWindow();
}

void UClipWindow::SetChildPosition(int newX, int newY)
{
	if (childH() == newX && childV() == newY)
		return;

	childH() = newX;
	childV() = newY;

	UWindow* child = GetChildWindow();
	if (!child)
		return;

	float x = bForceChildWidth() ? 0.0f : -hMult() * (float)childH();
	float y = bForceChildHeight() ? 0.0f : -vMult() * (float)childV();
	ClampChildPosition(x, y, child->Width(), child->Height());

	// Only the place of the child changes. The child keeps its size, so it needs no new configuration.
	child->X() = x;
	child->Y() = y;

	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ClipPositionChanged(this, childH(), childV()))
			break;
	}
}

void UClipWindow::GetChildPosition(int& pNewX, int& pNewY)
{
	pNewX = childH();
	pNewY = childV();
}

void UClipWindow::SetUnitSize(int hUnits, int vUnits)
{
	hUnits = std::max(hUnits, 1);
	vUnits = std::max(vUnits, 1);
	if (prefHUnits() != hUnits || prefVUnits() != vUnits)
	{
		prefHUnits() = hUnits;
		prefVUnits() = vUnits;
		AskParentForReconfigure();
	}
}

void UClipWindow::SetUnitWidth(int hUnits)
{
	hUnits = std::max(hUnits, 1);
	if (prefHUnits() != hUnits)
	{
		prefHUnits() = hUnits;
		AskParentForReconfigure();
	}
}

void UClipWindow::SetUnitHeight(int vUnits)
{
	vUnits = std::max(vUnits, 1);
	if (prefVUnits() != vUnits)
	{
		prefVUnits() = vUnits;
		AskParentForReconfigure();
	}
}

void UClipWindow::ResetUnitSize()
{
	if (prefHUnits() >= 0 || prefVUnits() >= 0)
	{
		prefHUnits() = -1;
		prefVUnits() = -1;
		AskParentForReconfigure();
	}
}

void UClipWindow::ResetUnitWidth()
{
	if (prefHUnits() >= 0)
	{
		prefHUnits() = -1;
		AskParentForReconfigure();
	}
}

void UClipWindow::ResetUnitHeight()
{
	if (prefVUnits() >= 0)
	{
		prefVUnits() = -1;
		AskParentForReconfigure();
	}
}

void UClipWindow::GetUnitSize(int& pAreaHSize, int& pAreaVSize, int& pChildHSize, int& pChildVSize)
{
	pAreaHSize = areaHSize();
	pAreaVSize = areaVSize();
	pChildHSize = childHSize();
	pChildVSize = childVSize();
}

void UClipWindow::ForceChildSize(std::optional<bool> bNewForceChildWidth, std::optional<bool> bNewForceChildHeight)
{
	// A child with a forced width or height gets that size of the clip window and does not move in that direction
	bool forceWidth = !bNewForceChildWidth || *bNewForceChildWidth;
	bool forceHeight = !bNewForceChildHeight || *bNewForceChildHeight;
	if (bForceChildWidth() != forceWidth || bForceChildHeight() != forceHeight)
	{
		bForceChildWidth() = forceWidth;
		bForceChildHeight() = forceHeight;
		AskParentForReconfigure();
	}
}

void UClipWindow::EnableSnapToUnits(std::optional<bool> bNewSnapToUnits)
{
	bool snap = !bNewSnapToUnits || *bNewSnapToUnits;
	if (bSnapToUnits() != snap)
	{
		bSnapToUnits() = snap;
		AskParentForReconfigure();
	}
}

// The size of one unit of the child in pixels
void UClipWindow::GetChildUnits(UWindow* child, float& hUnit, float& vUnit)
{
	hUnit = 1.0f;
	vUnit = 1.0f;
	if (bSnapToUnits() && child)
	{
		child->QueryGranularity(hUnit, vUnit);
		hUnit = std::max(hUnit, 1.0f);
		vUnit = std::max(vUnit, 1.0f);
	}
}

// The child cannot go further than its end at the end of the clip window, and not past its start
void UClipWindow::ClampChildPosition(float& x, float& y, float childWidth, float childHeight)
{
	x = std::max(x, Width() - childWidth);
	x = std::min(x, 0.0f);
	y = std::max(y, Height() - childHeight);
	y = std::min(y, 0.0f);
}

// The place and the size that the child wants for a size of the clip window
UClipWindow::ChildConfig UClipWindow::GetChildPreferredSize(UWindow* child, bool bWidthSpecified, float width, bool bHeightSpecified, float height)
{
	ChildConfig config;
	bool forceWidth = bWidthSpecified && bForceChildWidth();
	bool forceHeight = bHeightSpecified && bForceChildHeight();

	config.hPos = forceWidth ? 0 : childH();
	config.vPos = forceHeight ? 0 : childV();

	if (forceWidth && forceHeight)
	{
		config.width = width;
		config.height = height;
	}
	else if (forceWidth)
	{
		config.width = width;
		config.height = child->QueryPreferredHeight(width);
	}
	else if (forceHeight)
	{
		config.width = child->QueryPreferredWidth(height);
		config.height = height;
	}
	else
	{
		child->QueryPreferredSize(config.width, config.height);
	}
	return config;
}

// The size that the clip window wants. The child configuration that belongs to it goes to childConfig.
void UClipWindow::GetClipPreferredSize(UWindow* child, bool bWidthSpecified, float& width, bool bHeightSpecified, float& height, ChildConfig* childConfig)
{
	if (child)
	{
		float hUnit, vUnit;
		GetChildUnits(child, hUnit, vUnit);

		// The script can give the size as a count of units
		if (prefHUnits() >= 0 && !bWidthSpecified)
		{
			bWidthSpecified = true;
			width = (float)prefHUnits() * hUnit;
		}
		if (prefVUnits() >= 0 && !bHeightSpecified)
		{
			bHeightSpecified = true;
			height = (float)prefVUnits() * vUnit;
		}

		ChildConfig config = GetChildPreferredSize(child, bWidthSpecified, width, bHeightSpecified, height);
		if (!bWidthSpecified)
			width = config.width;
		if (!bHeightSpecified)
			height = config.height;
		if (childConfig)
			*childConfig = config;
	}

	UTabGroupWindow::ParentRequestedPreferredSize(bWidthSpecified, width, bHeightSpecified, height);
}

// For the scroll area: the size that the clip window wants, and whether the child is larger than that size
void UClipWindow::QueryClipPreferredSize(bool bWidthSpecified, float width, float& outWidth, bool& outChildWider, bool bHeightSpecified, float height, float& outHeight, bool& outChildHigher)
{
	// A size that the script set with SetSize counts as given
	if (!bWidthSpecified && FixedWidth)
	{
		width = hardcodedWidth();
		bWidthSpecified = true;
	}
	if (!bHeightSpecified && FixedHeight)
	{
		height = hardcodedHeight();
		bHeightSpecified = true;
	}

	float clipWidth = bWidthSpecified ? width : 10.0f;
	float clipHeight = bHeightSpecified ? height : 10.0f;

	UWindow* child = GetChildWindow();
	ChildConfig config;
	GetClipPreferredSize(child, bWidthSpecified, clipWidth, bHeightSpecified, clipHeight, &config);

	outChildWider = child && clipWidth < config.width;
	outChildHigher = child && clipHeight < config.height;

	outWidth = bWidthSpecified ? width : clipWidth;
	outHeight = bHeightSpecified ? height : clipHeight;
}

void UClipWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	GetClipPreferredSize(GetChildWindow(), bWidthSpecified, preferredWidth, bHeightSpecified, preferredHeight, nullptr);
}

void UClipWindow::ReconfigureChild(UWindow* child, int hPos, int vPos, float width, float height)
{
	GetChildUnits(child, hMult(), vMult());

	float x = -hMult() * (float)hPos;
	float y = -vMult() * (float)vPos;
	int childHUnits = (int)std::ceil(width / hMult());
	int childVUnits = (int)std::ceil(height / vMult());

	// A child that is smaller than the clip window fills it
	if (bFillWindow())
	{
		width = std::max(width, Width());
		height = std::max(height, Height());
	}

	// The units of the child that do not fit in the clip window
	int hiddenHUnits = (int)std::ceil((width - Width()) / hMult());
	int hiddenVUnits = (int)std::ceil((height - Height()) / vMult());
	ClampChildPosition(x, y, width, height);

	bool changed = childHSize() != childHUnits || childVSize() != childVUnits ||
		areaHSize() != childHUnits - hiddenHUnits || areaVSize() != childVUnits - hiddenVUnits;

	child->ConfigureChild(x, y, width, height);

	childHSize() = childHUnits;
	childVSize() = childVUnits;
	areaHSize() = childHUnits - hiddenHUnits;
	areaVSize() = childVUnits - hiddenVUnits;

	if (changed)
	{
		for (UWindow* cur = this; cur; cur = cur->parentOwner())
		{
			if (cur->ClipAttributesChanged(this, areaHSize(), areaVSize(), childHSize(), childVSize()))
				break;
		}
	}
}

void UClipWindow::ChildRequestedVisibilityChange(UWindow* childWin, bool bNewVisibility)
{
	childWin->SetChildVisibility(bNewVisibility);
	AskParentForReconfigure();
}

// The child asks to bring an area of it into view, for example the selected row of a list
void UClipWindow::ChildRequestedShowArea(UWindow* child, float showX, float showY, float showWidth, float showHeight)
{
	GetChildUnits(child, hMult(), vMult());

	// The area in units, and its start measured from the first unit in view
	int left = (int)std::floor((showX + 0.005f) / hMult());
	int right = (int)std::ceil((showX + 0.005f + (showWidth - 0.01f)) / hMult());
	int top = (int)std::floor((showY + 0.005f) / vMult());
	int bottom = (int)std::ceil((showY + 0.005f + (showHeight - 0.01f)) / vMult());

	int newH = childH() + ShowAreaMove(left - childH(), right - left, areaHSize());
	int newV = childV() + ShowAreaMove(top - childV(), bottom - top, areaVSize());
	SetChildPosition(newH, newV);
}

// How far the child must move so that an area comes into view. "start" is the first unit of the area measured
// from the first unit in view, "span" is its count of units, and "area" is the count of units in view.
int UClipWindow::ShowAreaMove(int start, int span, int area)
{
	if (area < span)
	{
		// The area is larger than the view: show as much of it as possible
		if (start < area - span)
			return span - area + start;
		if (start > 0)
			return start;
	}
	else
	{
		if (start > area - span)
			return span - area + start;
		if (start < 0)
			return start;
	}
	return 0;
}

void UClipWindow::ConfigurationChanged()
{
	UWindow* child = GetChildWindow();
	for (UWindow* cur = lastChild(); cur; cur = cur->prevSibling())
	{
		if (!cur->bIsVisible())
			continue;

		if (cur == child)
		{
			ChildConfig config = GetChildPreferredSize(cur, true, Width(), true, Height());
			ReconfigureChild(cur, config.hPos, config.vPos, config.width, config.height);
		}
		else
		{
			// A clip window shows only one child
			cur->ConfigureChild(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
}
