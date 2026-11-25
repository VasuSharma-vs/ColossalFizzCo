#pragma once

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"

namespace PrototypeMenuStyle
{
	inline const FLinearColor Cream(.94f, .91f, .82f);
	inline const FLinearColor Amber(1.f, .53f, .12f);
	inline const FLinearColor Muted(.59f, .65f, .69f);
	inline const FLinearColor Ink(.025f, .038f, .046f);

	inline void StyleButton(UButton* Button, bool bAccent)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(bAccent ? Amber : FLinearColor(.10f, .15f, .18f), 6.f));
		Style.SetHovered(FSlateRoundedBoxBrush(bAccent ? FLinearColor(1.f, .67f, .27f) : FLinearColor(.17f, .25f, .29f), 6.f));
		Style.SetPressed(FSlateRoundedBoxBrush(bAccent ? FLinearColor(.75f, .35f, .06f) : FLinearColor(.07f, .10f, .12f), 6.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.08f, .10f, .11f), 6.f));
		Button->SetStyle(Style);
	}
}
