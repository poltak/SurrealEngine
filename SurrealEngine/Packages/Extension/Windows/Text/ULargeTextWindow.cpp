
#include "Precomp.h"
#include "ULargeTextWindow.h"
#include "Engine.h"
#include "Packages/Engine/Resources/UFont.h"

void ULargeTextWindow::SetVerticalSpacing(std::optional<float> newVSpace)
{
	// UNUSED from scripts.
	LogUnimplemented("LargeTextWindow.SetVerticalSpacing");
}

// As XLargeTextWindow::ParentRequestedGranularity: the unit is the height of a line and the space after it
void ULargeTextWindow::ParentRequestedGranularity(float& hGranularity, float& vGranularity)
{
	if (UFont* font = normalFont())
	{
		hGranularity = 1.0f;
		vGranularity = std::max((float)font->GetGlyph('X').VSize, 1.0f) + vSpace();
	}
	UWindow::ParentRequestedGranularity(hGranularity, vGranularity);
}
