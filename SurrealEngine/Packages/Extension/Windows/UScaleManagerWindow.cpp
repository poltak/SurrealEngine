
#include "Precomp.h"
#include "UScaleManagerWindow.h"
#include "UScaleWindow.h"
#include "Text/UButtonWindow.h"
#include "Text/UTextWindow.h"

// A scale manager puts a scale, its two buttons and a text field with the value in a row or a column.
// The buttons move the thumb of the scale. The functions of this file follow XScaleManagerWindow in Extension.dll.

void UScaleManagerWindow::InitWindow()
{
	decButton() = nullptr;
	incButton() = nullptr;
	valueField() = nullptr;
	Scale() = nullptr;
	orientation() = (uint8_t)EOrientation::Horizontal;
	bStretchScaleField() = false;
	bStretchValueField() = true;
	marginWidth() = 0.0f;
	marginHeight() = 0.0f;
	Spacing() = 0.0f;
	childHAlign() = (uint8_t)EHAlign::Center;
	childVAlign() = (uint8_t)EVAlign::Center;

	UWindow::InitWindow();
}

bool UScaleManagerWindow::IsDescendant(UWindow* window)
{
	while (window && window != this)
		window = window->parentOwner();
	return window != nullptr;
}

void UScaleManagerWindow::SetManagerAlignments(uint8_t newHAlign, uint8_t newVAlign)
{
	if (childHAlign() != newHAlign || childVAlign() != newVAlign)
	{
		childHAlign() = newHAlign;
		childVAlign() = newVAlign;
		AskParentForReconfigure();
	}
}

void UScaleManagerWindow::SetManagerMargins(std::optional<float> newMarginWidth, std::optional<float> newMarginHeight)
{
	float width = newMarginWidth ? *newMarginWidth : 0.0f;
	float height = newMarginHeight ? *newMarginHeight : 0.0f;
	if (marginWidth() != width || marginHeight() != height)
	{
		marginWidth() = width;
		marginHeight() = height;
		AskParentForReconfigure();
	}
}

void UScaleManagerWindow::SetManagerOrientation(uint8_t newOrientation)
{
	orientation() = newOrientation;
	AskParentForReconfigure();
}

void UScaleManagerWindow::SetMarginSpacing(std::optional<float> newSpacing)
{
	float spacing = newSpacing ? *newSpacing : 0.0f;
	if (Spacing() != spacing)
	{
		Spacing() = spacing;
		AskParentForReconfigure();
	}
}

void UScaleManagerWindow::SetScale(UObject* NewScale)
{
	UScaleWindow* scale = UObject::Cast<UScaleWindow>(NewScale);
	if (!IsDescendant(scale))
		scale = nullptr;

	if (Scale() != scale)
	{
		Scale() = scale;
		AskParentForReconfigure();
		ChangeValueField();
	}
}

void UScaleManagerWindow::SetScaleButtons(UObject* newDecButton, UObject* newIncButton)
{
	UButtonWindow* dec = UObject::Cast<UButtonWindow>(newDecButton);
	UButtonWindow* inc = UObject::Cast<UButtonWindow>(newIncButton);
	decButton() = IsDescendant(dec) ? dec : nullptr;
	incButton() = IsDescendant(inc) ? inc : nullptr;
}

void UScaleManagerWindow::SetValueField(UObject* newValueField)
{
	UTextWindow* field = UObject::Cast<UTextWindow>(newValueField);
	if (!IsDescendant(field))
		field = nullptr;

	if (valueField() != field)
	{
		valueField() = field;
		AskParentForReconfigure();
		ChangeValueField();
	}
}

void UScaleManagerWindow::StretchScaleField(std::optional<bool> bNewStretch)
{
	bStretchScaleField() = !bNewStretch || *bNewStretch;
	AskParentForReconfigure();
}

void UScaleManagerWindow::StretchValueField(std::optional<bool> bNewStretch)
{
	bStretchValueField() = !bNewStretch || *bNewStretch;
	AskParentForReconfigure();
}

// Shows the value of the scale in the text field
void UScaleManagerWindow::ChangeValueField()
{
	if (valueField() && Scale())
		valueField()->SetText(Scale()->GetValueString());
}

void UScaleManagerWindow::ParentRequestedPreferredSize(bool bWidthSpecified, float& preferredWidth, bool bHeightSpecified, float& preferredHeight)
{
	bool vertical = (EOrientation)orientation() == EOrientation::Vertical;

	preferredWidth = 0.0f;
	preferredHeight = 0.0f;
	float gap = 0.0f;
	for (UWindow* child = UObject::Cast<UWindow>(GetBottomChild(true)); child; child = UObject::Cast<UWindow>(child->GetHigherSibling(true)))
	{
		float width = 0.0f, height = 0.0f;
		child->QueryPreferredSize(width, height);
		if (vertical)
		{
			preferredHeight += height + gap;
			preferredWidth = std::max(preferredWidth, width);
		}
		else
		{
			preferredWidth += width + gap;
			preferredHeight = std::max(preferredHeight, height);
		}
		gap = Spacing();
	}

	preferredWidth += marginWidth() * 2.0f;
	preferredHeight += marginHeight() * 2.0f;
}

void UScaleManagerWindow::ChildRequestedVisibilityChange(UWindow* childWin, bool bNewVisibility)
{
	childWin->SetChildVisibility(bNewVisibility);
	AskParentForReconfigure();
}

void UScaleManagerWindow::ConfigurationChanged()
{
	struct ChildConfig
	{
		UWindow* window;
		float x, y, width, height;
	};

	bool vertical = (EOrientation)orientation() == EOrientation::Vertical;
	float available = vertical ? Height() - marginHeight() : Width() - marginWidth();

	// Each child gets the size it wants. The children are one after the other along the manager.
	std::vector<ChildConfig> children;
	float end = 0.0f;
	float gap = vertical ? marginHeight() : marginWidth();
	for (UWindow* child = UObject::Cast<UWindow>(GetBottomChild(true)); child; child = UObject::Cast<UWindow>(child->GetHigherSibling(true)))
	{
		ChildConfig config = { child, 0.0f, 0.0f, 0.0f, 0.0f };
		child->QueryPreferredSize(config.width, config.height);

		if (vertical)
		{
			config.y = gap + end;
			switch ((EHAlign)childHAlign())
			{
			case EHAlign::Left: config.x = marginWidth(); break;
			case EHAlign::Center: config.x = (Width() - config.width) * 0.5f; break;
			case EHAlign::Right: config.x = Width() - config.width - marginWidth(); break;
			default: config.x = marginWidth(); config.width = Width() - marginWidth() * 2.0f; break;
			}
			end = config.y + config.height;
		}
		else
		{
			config.x = gap + end;
			switch ((EVAlign)childVAlign())
			{
			case EVAlign::Top: config.y = marginHeight(); break;
			case EVAlign::Center: config.y = (Height() - config.height) * 0.5f; break;
			case EVAlign::Bottom: config.y = Height() - config.height - marginHeight(); break;
			default: config.y = marginHeight(); config.height = Height() - marginHeight() * 2.0f; break;
			}
			end = config.x + config.width;
		}

		children.push_back(config);
		gap = Spacing();
	}

	// The scale and the value field can take the space that is left, or give back the space that is missing
	bool stretchScale = Scale() && Scale()->parentOwner() == this && Scale()->bIsVisible() && bStretchScaleField();
	bool stretchValue = valueField() && valueField()->parentOwner() == this && valueField()->bIsVisible() && bStretchValueField();
	if (end != available && (stretchScale || stretchValue))
	{
		float extra = available - end;
		float scaleExtra, valueExtra;
		if (!stretchScale)
		{
			scaleExtra = 0.0f;
			valueExtra = extra;
		}
		else if (!stretchValue)
		{
			scaleExtra = extra;
			valueExtra = 0.0f;
		}
		else
		{
			scaleExtra = extra * 0.5f;
			valueExtra = extra - scaleExtra;
		}

		float offset = 0.0f;
		for (ChildConfig& config : children)
		{
			float add = 0.0f;
			if (config.window == Scale())
				add = scaleExtra;
			else if (config.window == valueField())
				add = valueExtra;

			if (vertical)
			{
				config.y += offset;
				config.height += add;
			}
			else
			{
				config.x += offset;
				config.width += add;
			}
			offset += add;
		}
	}

	for (const ChildConfig& config : children)
		config.window->ConfigureChild(config.x, config.y, config.width, config.height);
}

void UScaleManagerWindow::ChildRemoved(UWindow* child)
{
	UWindow::ChildRemoved(child);

	if (child == decButton())
		decButton() = nullptr;
	if (child == incButton())
		incButton() = nullptr;
	if (child == Scale())
		Scale() = nullptr;
	if (child == valueField())
		valueField() = nullptr;
}

bool UScaleManagerWindow::ScalePositionChanged(UWindow* scale, int newTickPosition, float newValue, bool bFinal)
{
	bool handled = UWindow::ScalePositionChanged(scale, newTickPosition, newValue, bFinal);
	if (scale == Scale())
		ChangeValueField();
	return handled;
}

bool UScaleManagerWindow::ScaleAttributesChanged(UWindow* scale, int tickPosition, int tickSpan, int numTicks)
{
	bool handled = UWindow::ScaleAttributesChanged(scale, tickPosition, tickSpan, numTicks);
	if (scale == Scale())
	{
		// A button that cannot move the thumb further is not sensitive
		if (decButton())
			decButton()->SetSensitivity(tickPosition > 0);
		if (incButton())
			incButton()->SetSensitivity(tickPosition + tickSpan < numTicks);
	}
	return handled;
}

bool UScaleManagerWindow::ButtonActivated(UWindow* button)
{
	bool handled = UWindow::ButtonActivated(button);
	if (Scale())
	{
		if (button == incButton())
		{
			Scale()->MoveThumb((uint8_t)EMoveThumb::StepDown);
			handled = true;
		}
		else if (button == decButton())
		{
			Scale()->MoveThumb((uint8_t)EMoveThumb::StepUp);
			handled = true;
		}
	}
	return handled;
}
