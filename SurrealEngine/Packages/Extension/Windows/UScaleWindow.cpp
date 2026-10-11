#include "Precomp.h"
#include "UScaleWindow.h"
#include "UGC.h"
#include "Engine.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"

// A scale window is the slider of a settings screen or the bar of a scroll area. The thumb has a position from 0 to
// numPositions - 1. A thumb with a span (a scroll bar) covers spanRange positions and stops at numPositions - spanRange.
// The functions of this file follow XScaleWindow in Extension.dll.

void UScaleWindow::InitWindow()
{
	const Color white = { 255, 255, 255, 255 };

	orientation() = (uint8_t)EOrientation::Horizontal;
	scaleTexture() = nullptr;
	thumbTexture() = nullptr;
	tickTexture() = nullptr;
	preCapTexture() = nullptr;
	postCapTexture() = nullptr;
	bRepeatScaleTexture() = false;
	bRepeatThumbTexture() = false;
	bDrawEndTicks() = true;
	bStretchScale() = false;
	bSpanThumb() = false;
	scaleBorderSize() = 0.0f;
	thumbBorderSize() = 0.0f;
	scaleBorderColor() = white;
	thumbBorderColor() = white;
	scaleStyle() = (uint8_t)EDrawStyle::Masked;
	thumbStyle() = (uint8_t)EDrawStyle::Masked;
	tickStyle() = (uint8_t)EDrawStyle::Masked;
	scaleColor() = white;
	thumbColor() = white;
	tickColor() = white;
	scaleWidth() = 0.0f;
	scaleHeight() = 0.0f;
	ThumbWidth() = 0.0f;
	ThumbHeight() = 0.0f;
	tickWidth() = 0.0f;
	tickHeight() = 0.0f;
	preCapWidth() = 0.0f;
	preCapHeight() = 0.0f;
	postCapWidth() = 0.0f;
	postCapHeight() = 0.0f;
	startOffset() = 0.0f;
	endOffset() = 0.0f;
	marginWidth() = 0.0f;
	marginHeight() = 0.0f;
	numPositions() = 10;
	currentPos() = 0;
	spanRange() = 1;
	thumbStep() = 1;
	fromValue() = 0.0f;
	toValue() = 1.0f;
	valueFmt() = "%1.2f";
	initialDelay() = 0.5f;
	repeatRate() = 0.1f;
	initialPos() = -1;
	setSound() = nullptr;
	clickSound() = nullptr;
	dragSound() = nullptr;
	bDraggingThumb() = false;
	mousePos() = 0.0f;
	repeatDir() = 0;
	RemainingTime() = 0.0f;

	UWindow::InitWindow();
}

void UScaleWindow::SetScaleOrientation(uint8_t newOrientation)
{
	orientation() = newOrientation;
	AskParentForReconfigure();
}

void UScaleWindow::SetScaleTexture(UObject* NewTexture, std::optional<float> newWidth, std::optional<float> NewHeight, std::optional<float> newStart, std::optional<float> newEnd)
{
	scaleTexture() = UObject::Cast<UTexture>(NewTexture);
	scaleWidth() = std::max(newWidth ? *newWidth : 0.0f, 0.0f);
	scaleHeight() = std::max(NewHeight ? *NewHeight : 0.0f, 0.0f);
	startOffset() = newStart ? *newStart : 0.0f;
	endOffset() = newEnd ? *newEnd : 0.0f;
	AskParentForReconfigure();
}

void UScaleWindow::SetThumbTexture(UObject* NewTexture, std::optional<float> newWidth, std::optional<float> NewHeight)
{
	thumbTexture() = UObject::Cast<UTexture>(NewTexture);
	ThumbWidth() = std::max(newWidth ? *newWidth : 0.0f, 0.0f);
	ThumbHeight() = std::max(NewHeight ? *NewHeight : 0.0f, 0.0f);
	AskParentForReconfigure();
}

void UScaleWindow::SetTickTexture(UObject* newTickTexture, std::optional<bool> newDrawEndTicks, std::optional<float> newWidth, std::optional<float> NewHeight)
{
	tickTexture() = UObject::Cast<UTexture>(newTickTexture);
	tickWidth() = std::max(newWidth ? *newWidth : 0.0f, 0.0f);
	tickHeight() = std::max(NewHeight ? *NewHeight : 0.0f, 0.0f);
	bDrawEndTicks() = !newDrawEndTicks || *newDrawEndTicks;
	AskParentForReconfigure();
}

void UScaleWindow::SetThumbCaps(UObject* preCap, UObject* postCap, std::optional<float> newPreCapWidth, std::optional<float> newPreCapHeight, std::optional<float> newPostCapWidth, std::optional<float> newPostCapHeight)
{
	preCapTexture() = UObject::Cast<UTexture>(preCap);
	postCapTexture() = UObject::Cast<UTexture>(postCap);
	preCapWidth() = std::max(newPreCapWidth ? *newPreCapWidth : 0.0f, 0.0f);
	preCapHeight() = std::max(newPreCapHeight ? *newPreCapHeight : 0.0f, 0.0f);
	postCapWidth() = std::max(newPostCapWidth ? *newPostCapWidth : 0.0f, 0.0f);
	postCapHeight() = std::max(newPostCapHeight ? *newPostCapHeight : 0.0f, 0.0f);
	AskParentForReconfigure();
}

void UScaleWindow::EnableStretchedScale(std::optional<bool> bNewStretch)
{
	// The scale then has the length of the window, and its texture repeats along it
	bool stretch = !bNewStretch || *bNewStretch;
	if (bStretchScale() != stretch)
	{
		bStretchScale() = stretch;
		bRepeatScaleTexture() = stretch;
		AskParentForReconfigure();
	}
}

void UScaleWindow::SetBorderPattern(UObject* NewTexture)
{
	borderPattern() = UObject::Cast<UTexture>(NewTexture);
}

void UScaleWindow::SetScaleBorder(std::optional<float> newBorderSize, std::optional<Color> NewColor)
{
	float size = newBorderSize ? *newBorderSize : 0.0f;
	scaleBorderColor() = NewColor ? *NewColor : Color{ 255, 255, 255, 255 };
	if (scaleBorderSize() != size)
	{
		scaleBorderSize() = size;
		AskParentForReconfigure();
	}
}

void UScaleWindow::SetThumbBorder(std::optional<float> newBorderSize, std::optional<Color> NewColor)
{
	float size = newBorderSize ? *newBorderSize : 0.0f;
	thumbBorderColor() = NewColor ? *NewColor : Color{ 255, 255, 255, 255 };
	if (thumbBorderSize() != size)
	{
		thumbBorderSize() = size;
		AskParentForReconfigure();
	}
}

void UScaleWindow::SetScaleStyle(uint8_t NewStyle)
{
	scaleStyle() = NewStyle;
}

void UScaleWindow::SetThumbStyle(uint8_t NewStyle)
{
	thumbStyle() = NewStyle;
}

void UScaleWindow::SetTickStyle(uint8_t NewStyle)
{
	tickStyle() = NewStyle;
}

void UScaleWindow::SetScaleColor(const Color& NewColor)
{
	scaleColor() = NewColor;
}

void UScaleWindow::SetThumbColor(const Color& NewColor)
{
	thumbColor() = NewColor;
}

void UScaleWindow::SetTickColor(const Color& NewColor)
{
	tickColor() = NewColor;
}

void UScaleWindow::SetScaleMargins(std::optional<float> newMarginWidth, std::optional<float> newMarginHeight)
{
	float width = std::max(newMarginWidth ? *newMarginWidth : 0.0f, 0.0f);
	float height = std::max(newMarginHeight ? *newMarginHeight : 0.0f, 0.0f);
	if (marginWidth() != width || marginHeight() != height)
	{
		marginWidth() = width;
		marginHeight() = height;
		AskParentForReconfigure();
	}
}

void UScaleWindow::SetNumTicks(int newNumTicks)
{
	newNumTicks = std::max(newNumTicks, 1);
	if (numPositions() != newNumTicks)
	{
		numPositions() = newNumTicks;
		ChangeThumbPosition(currentPos(), true, true);
	}
}

int UScaleWindow::GetNumTicks()
{
	return numPositions();
}

void UScaleWindow::SetThumbSpan(std::optional<int> newRange)
{
	// A span of 0 gives a slider. A larger span gives the thumb of a scroll bar, with a texture that repeats.
	int range = newRange ? *newRange : 1;
	bool span = range > 0;
	if (spanRange() != range || bSpanThumb() != span)
	{
		bSpanThumb() = span;
		bRepeatThumbTexture() = span;
		if (span)
			spanRange() = range;
		ChangeThumbPosition(currentPos(), true, true);
	}
}

int UScaleWindow::GetThumbSpan()
{
	return bSpanThumb() ? spanRange() : 0;
}

void UScaleWindow::SetRanges(int newSpan, int newNumTicks)
{
	newSpan = std::max(newSpan, 1);
	if (spanRange() != newSpan || numPositions() != newNumTicks)
	{
		spanRange() = newSpan;
		numPositions() = newNumTicks;
		ChangeThumbPosition(currentPos(), true, true);
	}
}

void UScaleWindow::SetTickPosition(int newPosition)
{
	ChangeThumbPosition(newPosition, false, true);
}

int UScaleWindow::GetTickPosition()
{
	return currentPos();
}

void UScaleWindow::SetValueRange(float newFrom, float newTo)
{
	if (fromValue() != newFrom || toValue() != newTo)
	{
		fromValue() = newFrom;
		toValue() = newTo;
		ChangeScalePosition(true);
	}
}

void UScaleWindow::SetValue(float NewValue)
{
	ChangeThumbPosition(ValueToTick(NewValue), false, true);
}

float UScaleWindow::GetValue()
{
	return TickToValue(currentPos());
}

void UScaleWindow::GetValues(float& outFromValue, float& outToValue)
{
	outFromValue = TickToValue(currentPos());
	if (bSpanThumb())
		outToValue = TickToValue(spanRange() + currentPos());
	else
		outToValue = fromValue(); // The original gives the start of the value range here
}

void UScaleWindow::SetValueFormat(const std::string& newFmt)
{
	valueFmt() = newFmt;
	ChangeScalePosition(true);
}

std::string UScaleWindow::GetValueString()
{
	int pos = currentPos();
	if (pos >= 0 && (size_t)pos < enumStrings.size() && !enumStrings[(size_t)pos].empty())
		return enumStrings[(size_t)pos];

	// The format is a printf format for one floating point value. Other conversions get the value as an integer,
	// so a format from a script cannot read memory that is not there.
	const std::string& format = valueFmt();
	float value = GetValue();
	std::string result;
	bool valueUsed = false;
	for (size_t i = 0; i < format.size(); i++)
	{
		if (format[i] != '%')
		{
			result += format[i];
			continue;
		}

		size_t start = i++;
		while (i < format.size() && std::strchr("-+ #0123456789.", format[i]))
			i++;
		if (i >= format.size())
			break;

		char conversion = format[i];
		std::string spec = format.substr(start, i - start);
		char buffer[128];
		if (conversion == '%')
		{
			result += '%';
		}
		else if (std::strchr("fFeEgG", conversion) && !valueUsed)
		{
			std::snprintf(buffer, sizeof(buffer), (spec + conversion).c_str(), (double)value);
			result += buffer;
			valueUsed = true;
		}
		else if (std::strchr("diu", conversion) && !valueUsed)
		{
			std::snprintf(buffer, sizeof(buffer), (spec + "d").c_str(), (int)value);
			result += buffer;
			valueUsed = true;
		}
	}
	return result;
}

void UScaleWindow::SetEnumeration(int tickPos, const std::string& newStr)
{
	if (tickPos < 0 || tickPos >= 512)
		return;

	if (enumStrings.size() <= (size_t)tickPos)
		enumStrings.resize((size_t)tickPos + 1);
	enumStrings[(size_t)tickPos] = newStr;

	if (tickPos == currentPos())
		ChangeScalePosition(true);
}

void UScaleWindow::ClearAllEnumerations()
{
	enumStrings.clear();
	ChangeScalePosition(true);
}

void UScaleWindow::MoveThumb(uint8_t MoveThumb)
{
	int page = bSpanThumb() ? spanRange() : 4;
	int newPos = 0;
	switch ((EMoveThumb)MoveThumb)
	{
	default:
	case EMoveThumb::Home: newPos = 0; break;
	case EMoveThumb::End: newPos = numPositions(); break;
	case EMoveThumb::Prev: newPos = currentPos() - 1; break;
	case EMoveThumb::Next: newPos = currentPos() + 1; break;
	case EMoveThumb::StepUp: newPos = currentPos() - thumbStep(); break;
	case EMoveThumb::StepDown: newPos = currentPos() + thumbStep(); break;
	case EMoveThumb::PageUp: newPos = currentPos() - page; break;
	case EMoveThumb::PageDown: newPos = currentPos() + page; break;
	}
	ChangeThumbPosition(newPos, false, true);
}

void UScaleWindow::SetThumbStep(int NewStep)
{
	thumbStep() = std::max(NewStep, 1);
}

void UScaleWindow::SetScaleSounds(std::optional<UObject*> newSetSound, std::optional<UObject*> newClickSound, std::optional<UObject*> newDragSound)
{
	setSound() = newSetSound ? UObject::Cast<USound>(*newSetSound) : nullptr;
	clickSound() = newClickSound ? UObject::Cast<USound>(*newClickSound) : nullptr;
	dragSound() = newDragSound ? UObject::Cast<USound>(*newDragSound) : nullptr;
}

void UScaleWindow::PlayScaleSound(UObject* newsound, std::optional<float> Volume, std::optional<float> Pitch)
{
	PlaySound(newsound, Volume, Pitch, thumbW() * 0.5f + thumbX(), thumbH() * 0.5f + thumbY());
}

// The pixel in the window, along the scale, where the thumb starts for a tick position.
// For a slider this is the middle of the thumb.
float UScaleWindow::TickToPixel(int tick)
{
	float range;
	int count;
	if (bSpanThumb())
	{
		range = absEndScale() - absStartScale() + 1.0f - (IsVertical() ? thumbH() : thumbW());
		count = numPositions() - spanRange();
	}
	else
	{
		range = absEndScale() - absStartScale();
		count = numPositions() - 1;
	}
	count = std::max(count, 1);
	return (float)(int)((float)tick * range / (float)count + absStartScale() + 0.5f);
}

int UScaleWindow::PixelToTick(float pixel)
{
	float range;
	int count;
	if (bSpanThumb())
	{
		range = absEndScale() - absStartScale() + 1.0f - (IsVertical() ? thumbH() : thumbW());
		count = numPositions() - spanRange();
	}
	else
	{
		range = absEndScale() - absStartScale();
		count = numPositions() - 1;
	}
	range = std::max(range, 1.0f);
	return (int)((pixel - absStartScale()) * (float)count / range + 0.5f);
}

float UScaleWindow::TickToValue(int tick)
{
	int count = bSpanThumb() ? numPositions() : numPositions() - 1;
	if (count > 0)
		return (toValue() - fromValue()) * (float)tick / (float)count + fromValue();
	return (toValue() - fromValue()) * 0.5f + fromValue();
}

int UScaleWindow::ValueToTick(float value)
{
	int count = bSpanThumb() ? numPositions() : numPositions() - 1;
	float tick = 0.0f;
	if (toValue() != fromValue())
		tick = (value - fromValue()) * (float)count / (toValue() - fromValue());
	return (tick < 0.0f) ? (int)(tick - 0.5f) : (int)(tick + 0.5f);
}

// The size of a part: the given size, or the size of its texture where the given size is 0. The border is on both sides.
void UScaleWindow::ComputeTextureSize(UTexture* texture, float width, float height, float border, float& outWidth, float& outHeight)
{
	if (width > 0.0f)
		outWidth = width;
	else
		outWidth = texture ? (float)texture->USize() : 0.0f;

	if (height > 0.0f)
		outHeight = height;
	else
		outHeight = texture ? (float)texture->VSize() : 0.0f;

	outWidth += border * 2.0f;
	outHeight += border * 2.0f;
}

void UScaleWindow::ComputeThumbConfig()
{
	ComputeTextureSize(thumbTexture(), ThumbWidth(), ThumbHeight(), thumbBorderSize(), thumbW(), thumbH());

	preCapXOff() = 0.0f;
	preCapYOff() = 0.0f;
	postCapXOff() = 0.0f;
	postCapYOff() = 0.0f;
	ComputeTextureSize(preCapTexture(), preCapWidth(), preCapHeight(), 0.0f, preCapW(), preCapH());
	ComputeTextureSize(postCapTexture(), postCapWidth(), postCapHeight(), 0.0f, postCapW(), postCapH());

	bool vertical = IsVertical();
	float border2 = thumbBorderSize() * 2.0f;
	if (vertical)
	{
		thumbH() += preCapH() + postCapH();
		preCapW() = thumbW() - border2;
		postCapW() = thumbW() - border2;
	}
	else
	{
		thumbW() += preCapW() + postCapW();
		preCapH() = thumbH() - border2;
		postCapH() = thumbH() - border2;
	}

	thumbX() = (float)(int)((Width() - thumbW()) * 0.5f);
	thumbY() = (float)(int)((Height() - thumbH()) * 0.5f);

	if (!bSpanThumb())
	{
		// A slider: the middle of the thumb is on the tick
		if (vertical)
			thumbY() = (float)(int)(TickToPixel(currentPos()) - thumbH() * 0.5f + 0.5f);
		else
			thumbX() = (float)(int)(TickToPixel(currentPos()) - thumbW() * 0.5f + 0.5f);
		return;
	}

	// A scroll bar: the length of the thumb shows how much of the positions the span covers.
	// It is 6 pixels or more and not longer than the scale.
	float scaleLength = absEndScale() - absStartScale() + 1.0f;
	float length = (float)(int)(scaleLength * (float)spanRange() / (float)std::max(numPositions(), 1) + 0.5f);
	length = std::max(length, 6.0f);
	length = std::min(length, scaleLength);

	float& thumbLength = vertical ? thumbH() : thumbW();
	float& preCapLength = vertical ? preCapH() : preCapW();
	float& postCapLength = vertical ? postCapH() : postCapW();
	float& postCapOffset = vertical ? postCapYOff() : postCapXOff();
	thumbLength = length;

	// The two caps get shorter when the thumb has no space for them
	float missing = border2 + preCapLength + postCapLength - thumbLength;
	if (missing > 0.0f)
	{
		float half = (float)(int)(missing * 0.5f);
		preCapLength -= missing - half;
		postCapLength -= half;
		postCapOffset += half;
	}

	if (vertical)
		thumbY() = TickToPixel(currentPos());
	else
		thumbX() = TickToPixel(currentPos());
}

void UScaleWindow::ChangeThumbPosition(int newPos, bool force, bool final)
{
	int maxPos = bSpanThumb() ? numPositions() - spanRange() : numPositions() - 1;
	newPos = std::min(newPos, maxPos);
	newPos = std::max(newPos, 0);

	if (currentPos() != newPos)
	{
		force = true;
		currentPos() = newPos;
		ComputeThumbConfig();
		ChangeScalePosition(final);
	}

	if (force)
	{
		ChangeScaleAttributes();
		ComputeThumbConfig();
	}
}

// Tells the windows above the scale where the thumb is
void UScaleWindow::ChangeScalePosition(bool final)
{
	if (!bSpanThumb())
	{
		int pos = currentPos();
		float value = TickToValue(pos);
		for (UWindow* cur = this; cur; cur = cur->parentOwner())
		{
			if (cur->ScalePositionChanged(this, pos, value, final))
				break;
		}
	}
	else
	{
		int fromTick = currentPos();
		int toTick = fromTick + spanRange();
		float from = TickToValue(fromTick);
		float to = TickToValue(toTick);
		for (UWindow* cur = this; cur; cur = cur->parentOwner())
		{
			if (cur->ScaleRangeChanged(this, fromTick, toTick, from, to, final))
				break;
		}
	}
}

void UScaleWindow::ChangeScaleAttributes()
{
	if (!IsVisible({}))
		return;

	for (UWindow* cur = this; cur; cur = cur->parentOwner())
	{
		if (cur->ScaleAttributesChanged(this, currentPos(), spanRange(), numPositions()))
			break;
	}
}

void UScaleWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	float width = 0.0f, height = 0.0f;
	ComputeTextureSize(scaleTexture(), scaleWidth(), scaleHeight(), scaleBorderSize(), preferredWidth, preferredHeight);

	ComputeTextureSize(thumbTexture(), ThumbWidth(), ThumbHeight(), thumbBorderSize(), width, height);
	preferredWidth = std::max(preferredWidth, width);
	preferredHeight = std::max(preferredHeight, height);

	ComputeTextureSize(tickTexture(), tickWidth(), tickHeight(), 0.0f, width, height);
	preferredWidth = std::max(preferredWidth, width);
	preferredHeight = std::max(preferredHeight, height);

	preferredWidth += marginWidth() * 2.0f;
	preferredHeight += marginHeight() * 2.0f;
}

void UScaleWindow::ConfigurationChanged()
{
	ComputeTextureSize(scaleTexture(), scaleWidth(), scaleHeight(), scaleBorderSize(), scaleW(), scaleH());

	bool vertical = IsVertical();
	if (bStretchScale())
	{
		if (vertical)
			scaleH() = Height() - marginHeight() * 2.0f;
		else
			scaleW() = Width() - marginWidth() * 2.0f;
	}

	scaleX() = (float)(int)((Width() - scaleW()) * 0.5f);
	scaleY() = (float)(int)((Height() - scaleH()) * 0.5f);

	// The first and the last pixel along the scale that the thumb can use
	float start = vertical ? scaleY() : scaleX();
	float length = vertical ? scaleH() : scaleW();
	absStartScale() = start + startOffset() + scaleBorderSize();
	absEndScale() = start + length - (endOffset() + 1.0f) - scaleBorderSize();
	if (absEndScale() < absStartScale())
	{
		float middle = absStartScale() + (absEndScale() - absStartScale()) * 0.5f;
		absStartScale() = middle;
		absEndScale() = middle;
	}

	ComputeThumbConfig();

	ComputeTextureSize(tickTexture(), tickWidth(), tickHeight(), 0.0f, tickW(), tickH());
	tickX() = (float)(int)((Width() - tickW()) * 0.5f);
	tickY() = (float)(int)((Height() - tickH()) * 0.5f);
}

// As XGC::DrawIconPattern. A source size of 0 repeats the texture in that direction. With a source size, the texture
// shows one time at its own scale, and the part of the area that is outside of the texture stays empty.
void UScaleWindow::DrawIconPattern(UGC* gc, float x, float y, float w, float h, float orgX, float orgY, float srcW, float srcH, UTexture* texture)
{
	if (!texture)
		return;
	if (srcW != 0.0f)
		w = std::min(w, (float)texture->USize() - orgX);
	if (srcH != 0.0f)
		h = std::min(h, (float)texture->VSize() - orgY);
	if (w > 0.0f && h > 0.0f)
		gc->DrawPattern(x, y, w, h, orgX, orgY, texture);
}

void UScaleWindow::DrawScaleTexture(UGC* gc, UTexture* texture, float x, float y, float w, float h, bool repeat, uint8_t style, const Color& color, float borderSize, const Color& borderColor, const ThumbCaps& caps)
{
	bool vertical = IsVertical();

	gc->SetStyle((EDrawStyle)style);

	if (borderSize > 0.0f)
	{
		if (borderPattern())
		{
			gc->SetTileColor(borderColor);
			gc->DrawBox(x, y, w, h, 0.0f, 0.0f, borderSize, borderPattern());
		}
		x += borderSize;
		y += borderSize;
		w -= borderSize * 2.0f;
		h -= borderSize * 2.0f;
	}

	gc->SetTileColor(color);

	if (caps.preCap && caps.preW > 0.0f && caps.preH > 0.0f && w > 0.0f && h > 0.0f)
	{
		if (vertical)
		{
			DrawIconPattern(gc, x, y, caps.preW, caps.preH, caps.preXOff, caps.preYOff, 0.0f, caps.preH, caps.preCap);
			y += caps.preH;
			h -= caps.preH;
		}
		else
		{
			DrawIconPattern(gc, x, y, caps.preW, caps.preH, caps.preXOff, caps.preYOff, caps.preW, 0.0f, caps.preCap);
			x += caps.preW;
			w -= caps.preW;
		}
	}

	if (caps.postCap && caps.postW > 0.0f && caps.postH > 0.0f && w > 0.0f && h > 0.0f)
	{
		if (vertical)
		{
			DrawIconPattern(gc, x, y + h - caps.postH, caps.postW, caps.postH, caps.postXOff, caps.postYOff, 0.0f, caps.postH, caps.postCap);
			h -= caps.postH;
		}
		else
		{
			DrawIconPattern(gc, x + w - caps.postW, y, caps.postW, caps.postH, caps.postXOff, caps.postYOff, caps.postW, 0.0f, caps.postCap);
			w -= caps.postW;
		}
	}

	if (texture && w > 0.0f && h > 0.0f)
	{
		// Along the scale the texture repeats or shows one time. Across the scale it always repeats.
		float srcW = 0.0f, srcH = 0.0f;
		if (!repeat)
		{
			if (vertical)
				srcH = h;
			else
				srcW = w;
		}
		DrawIconPattern(gc, x, y, w, h, 0.0f, 0.0f, srcW, srcH, texture);
	}
}

void UScaleWindow::DrawWindow(UGC* gc)
{
	DrawRawBackground(gc);

	DrawScaleTexture(gc, scaleTexture(), scaleX(), scaleY(), scaleW(), scaleH(), bRepeatScaleTexture(), scaleStyle(), scaleColor(), scaleBorderSize(), scaleBorderColor(), {});

	if (tickTexture())
	{
		int first = 0;
		int count = numPositions();
		if (bSpanThumb())
			count++;
		if (!bDrawEndTicks())
		{
			first = 1;
			count--;
		}

		bool vertical = IsVertical();
		for (int i = first; i < count; i++)
		{
			float pixel = TickToPixel(i);
			if (vertical)
				DrawScaleTexture(gc, tickTexture(), tickX(), pixel - (float)(int)(tickH() * 0.5f), tickW(), tickH(), false, tickStyle(), tickColor(), 0.0f, {}, {});
			else
				DrawScaleTexture(gc, tickTexture(), pixel - (float)(int)(tickW() * 0.5f), tickY(), tickW(), tickH(), false, tickStyle(), tickColor(), 0.0f, {}, {});
		}
	}

	ThumbCaps caps;
	caps.preCap = preCapTexture();
	caps.postCap = postCapTexture();
	caps.preXOff = preCapXOff();
	caps.preYOff = preCapYOff();
	caps.preW = preCapW();
	caps.preH = preCapH();
	caps.postXOff = postCapXOff();
	caps.postYOff = postCapYOff();
	caps.postW = postCapW();
	caps.postH = postCapH();
	DrawScaleTexture(gc, thumbTexture(), thumbX(), thumbY(), thumbW(), thumbH(), bRepeatThumbTexture(), thumbStyle(), thumbColor(), thumbBorderSize(), thumbBorderColor(), caps);
}

bool UScaleWindow::MouseButtonPressed(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UWindow::MouseButtonPressed(pointX, pointY, button, numClicks);
	if (button != IK_LeftMouse)
		return handled;

	bool vertical = IsVertical();
	bool onThumb = pointX >= thumbX() && pointX < thumbX() + thumbW() && pointY >= thumbY() && pointY < thumbY() + thumbH();
	if (onThumb)
	{
		// Start to move the thumb with the mouse
		bDraggingThumb() = true;
		initialPos() = currentPos();
		mousePos() = vertical ? pointY - thumbY() : pointX - thumbX();
		PlayScaleSound(clickSound(), {}, {});
	}
	else if (!bSpanThumb())
	{
		// A slider: the thumb goes to the mouse and then moves with it
		initialPos() = currentPos();
		bDraggingThumb() = true;
		ChangeThumbPosition(PixelToTick(vertical ? pointY : pointX), false, false);
		PlayScaleSound(setSound(), {}, {});
	}
	else
	{
		// A scroll bar: go one page in the direction of the mouse, and again while the player holds the button
		bool after = vertical ? thumbY() <= pointY : thumbX() <= pointX;
		repeatDir() = after ? 1 : 2;
		MoveThumb((uint8_t)(after ? EMoveThumb::PageDown : EMoveThumb::PageUp));
		PlayScaleSound(setSound(), {}, {});

		RemainingTime() = initialDelay();
		if (RemainingTime() <= 0.0f)
			RemainingTime() = repeatRate();
		if (RemainingTime() <= 0.0f)
			repeatDir() = 0;
	}

	// The mouse stays with this window until the player releases the button
	GrabMouse();
	grabbedMouse = true;
	return true;
}

void UScaleWindow::MouseMoved(float newX, float newY)
{
	UWindow::MouseMoved(newX, newY);

	if (!bDraggingThumb())
		return;

	float pixel = IsVertical() ? newY : newX;
	if (bSpanThumb())
		pixel -= mousePos();

	int oldPos = currentPos();
	ChangeThumbPosition(PixelToTick(pixel), false, false);
	if (oldPos != currentPos())
		PlayScaleSound(dragSound(), {}, {});
}

bool UScaleWindow::MouseButtonReleased(float pointX, float pointY, EInputKey button, int numClicks)
{
	bool handled = UWindow::MouseButtonReleased(pointX, pointY, button, numClicks);
	if (button != IK_LeftMouse)
		return handled;

	if (grabbedMouse)
	{
		UngrabMouse();
		grabbedMouse = false;
	}

	if (bDraggingThumb() && currentPos() != initialPos())
	{
		// The last position of a move with the mouse is the final one
		ChangeScalePosition(true);
		PlayScaleSound(setSound(), {}, {});
	}
	repeatDir() = 0;
	bDraggingThumb() = false;
	return true;
}

void UScaleWindow::Tick(float timeElapsed)
{
	if (repeatDir() != 0)
	{
		RemainingTime() -= timeElapsed;
		if (RemainingTime() < 0.0f)
		{
			float mouseX = 0.0f, mouseY = 0.0f;
			GetCursorPos(mouseX, mouseY);
			if (mouseX >= 0.0f && mouseX < Width() && mouseY >= 0.0f && mouseY < Height())
			{
				// The thumb goes on only while it is not yet at the mouse
				int oldPos = currentPos();
				if (IsVertical())
				{
					if (mouseY < thumbY() && repeatDir() == 2)
						MoveThumb((uint8_t)EMoveThumb::PageUp);
					if (thumbY() + thumbH() <= mouseY && repeatDir() == 1)
						MoveThumb((uint8_t)EMoveThumb::PageDown);
				}
				else
				{
					if (mouseX < thumbX() && repeatDir() == 2)
						MoveThumb((uint8_t)EMoveThumb::PageUp);
					if (thumbX() + thumbW() <= mouseX && repeatDir() == 1)
						MoveThumb((uint8_t)EMoveThumb::PageDown);
				}
				if (oldPos != currentPos())
					PlayScaleSound(setSound(), {}, {});
			}

			RemainingTime() += repeatRate();
			if (RemainingTime() < 0.0f)
				RemainingTime() = 0.05f;
		}
	}

	UWindow::Tick(timeElapsed);
}
