
#include "Precomp.h"
#include "UCheckboxWindow.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Engine.h"

void UCheckboxWindow::SetCheckboxColor(const Color& NewColor)
{
	checkboxColor() = NewColor;
}

void UCheckboxWindow::SetCheckboxSpacing(float newSpacing)
{
	checkboxSpacing() = newSpacing;
}

void UCheckboxWindow::SetCheckboxStyle(uint8_t NewStyle)
{
	checkboxStyle() = NewStyle;
}

void UCheckboxWindow::SetCheckboxTextures(std::optional<UObject*> newToggleOff, std::optional<UObject*> newToggleOn, std::optional<float> newTextureWidth, std::optional<float> newTextureHeight)
{
	if (newToggleOff)
		toggleOff() = UObject::Cast<UTexture>(*newToggleOff);
	if (newToggleOn)
		toggleOn() = UObject::Cast<UTexture>(*newToggleOn);
	if (newTextureWidth)
		textureWidth() = *newTextureWidth;
	if (newTextureHeight)
		textureHeight() = *newTextureHeight;
}

void UCheckboxWindow::ShowCheckboxOnRightSide(std::optional<bool> bRight)
{
	bRightSide() = !bRight || *bRight;
}

void UCheckboxWindow::InitWindow()
{
	// As XCheckboxWindow::Init
	textureWidth() = 0.0f;
	textureHeight() = 0.0f;
	bRightSide() = false;
	checkboxColor() = { 255, 255, 255, 255 };
	checkboxSpacing() = 3.0f;
	checkboxStyle() = (uint8_t)EDrawStyle::Masked;

	UToggleWindow::InitWindow();
}

// The size of the checkbox picture, and the width that the picture and the space to the text use
void UCheckboxWindow::ComputeTextureSize(float& width, float& height, float& usedWidth)
{
	if (textureWidth() > 0.0f)
	{
		width = textureWidth();
	}
	else
	{
		width = 0.0f;
		if (toggleOn())
			width = std::max(width, (float)toggleOn()->USize());
		if (toggleOff())
			width = std::max(width, (float)toggleOff()->USize());
	}

	if (textureHeight() > 0.0f)
	{
		height = textureHeight();
	}
	else
	{
		height = 0.0f;
		if (toggleOn())
			height = std::max(height, (float)toggleOn()->VSize());
		if (toggleOff())
			height = std::max(height, (float)toggleOff()->VSize());
	}

	usedWidth = (width > 0.0f) ? width + checkboxSpacing() : 0.0f;
}

// As XCheckboxWindow::Draw
void UCheckboxWindow::DrawWindow(UGC* gc)
{
	DrawRawBackground(gc);
	gc->SetAlignments(HAlign(), VAlign());
	DrawWindowEvent(gc);

	UTexture* tex = GetToggle() ? toggleOn() : toggleOff();

	float texWidth = 0.0f, texHeight = 0.0f, usedWidth = 0.0f;
	ComputeTextureSize(texWidth, texHeight, usedWidth);

	float xMargin = hMargin();
	float yMargin = vMargin();
	float iconY = (float)(int)((Height() - texHeight) * 0.5f);
	float textWidth = Width() - (xMargin * 2.0f + usedWidth);
	float textHeight = Height() - yMargin * 2.0f;

	float textX, iconX;
	if (!bRightSide())
	{
		textX = usedWidth + xMargin;
		iconX = xMargin;
	}
	else
	{
		textX = xMargin;
		iconX = Width() - (usedWidth + xMargin);
	}

	if (normalFont() && textWidth > 0.0f && textHeight > 0.0f)
	{
		gc->SetTextColor(curTextColor());
		gc->DrawText(textX, yMargin + 1.0f, textWidth, textHeight, Text());
	}

	if (tex)
	{
		// The picture shows one time at its own size. A picture that is smaller than the given size is not stretched.
		float drawWidth = std::min(texWidth, (float)tex->USize());
		float drawHeight = std::min(texHeight, (float)tex->VSize());
		gc->SetStyle((EDrawStyle)checkboxStyle());
		gc->SetTileColor(checkboxColor());
		gc->DrawPattern(iconX, iconY, drawWidth, drawHeight, 0.0f, 0.0f, tex);
	}
}

void UCheckboxWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	float xExtent = 0.0f, yExtent = 0.0f;
	UObject* oldNormalFont = nullptr;
	UObject* oldBoldFont = nullptr;
	engine->dxgc->GetFonts(oldNormalFont, oldBoldFont);
	engine->dxgc->SetFonts(normalFont(), boldFont());
	engine->dxgc->GetTextExtent(bWidthSpecified ? std::max(preferredWidth, MinWidth()) : 100000.0f, xExtent, yExtent, Text());
	engine->dxgc->SetFonts(oldNormalFont, oldBoldFont);

	if (UFont* font = normalFont())
	{
		int lineHeight = font->GetGlyph('X').VSize;
		int minHeight = minLines() * lineHeight;
		int maxHeight = MaxLines() * lineHeight;
		if (maxHeight > 0 && minHeight <= maxHeight)
			yExtent = std::clamp(yExtent, (float)minHeight, (float)maxHeight);
	}

	xExtent = std::max(xExtent, MinWidth());

	// The text gets less space by the width of the picture and the space next to it. The window is not lower than the picture.
	float texWidth = 0.0f, texHeight = 0.0f, usedWidth = 0.0f;
	ComputeTextureSize(texWidth, texHeight, usedWidth);
	xExtent += usedWidth;
	yExtent = std::max(yExtent, texHeight);

	float xMargin = hMargin();
	float yMargin = vMargin();
	if (!bWidthSpecified)
		preferredWidth = xExtent + xMargin * 2.0f;
	if (!bHeightSpecified)
		preferredHeight = yExtent + yMargin * 2.0f;

	UWindow::ParentRequestedPreferredSize(bWidthSpecified, preferredWidth, bHeightSpecified, preferredHeight);
}
