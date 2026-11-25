#include "UI/ShopClockWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ShopClockHUD"

TSharedRef<SWidget> UShopClockWidget::RebuildWidget()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);

	if (!WidgetTree->RootWidget)
	{
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f, .065f, .08f, .95f), 10.f));
		Card->SetPadding(FMargin(14.f, 7.f));
		auto* Placement = Canvas->AddChildToCanvas(Card);
		Placement->SetAnchors(FAnchors(.5f, 0.f));
		Placement->SetAlignment(FVector2D(.5f, 0.f));
		Placement->SetPosition(FVector2D(0.f, 18.f));
		Placement->SetAutoSize(true);

		auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Content);
		auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		Content->AddChildToVerticalBox(Header)->SetHorizontalAlignment(HAlign_Center);

		DayText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShopClockDay"));
		DayText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
		DayText->SetColorAndOpacity(FLinearColor(.94f, .7f, .36f));
		auto* DaySlot = Header->AddChildToHorizontalBox(DayText);
		DaySlot->SetVerticalAlignment(VAlign_Center);
		DaySlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));

		TimeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShopClockTime"));
		TimeText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18));
		TimeText->SetColorAndOpacity(FLinearColor(.58f, .94f, .83f));
		Header->AddChildToHorizontalBox(TimeText)->SetVerticalAlignment(VAlign_Center);

		PhaseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShopClockPhase"));
		PhaseText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
		Content->AddChildToVerticalBox(PhaseText)->SetHorizontalAlignment(HAlign_Center);
	}
	RefreshReadout();
	return Super::RebuildWidget();
}

void UShopClockWidget::UpdateClock(const FShopClockState& State)
{
	if (State == ClockState) return;
	ClockState = State;
	RefreshReadout();
}

void UShopClockWidget::RefreshReadout()
{
	if (!DayText || !TimeText || !PhaseText) return;

	DayText->SetText(FText::Format(LOCTEXT("Day", "Day {0}"), FText::AsNumber(ClockState.DayNumber)));
	const int32 Hour = ClockState.MinuteOfDay / 60;
	const int32 HourOnClock = Hour % 12 == 0 ? 12 : Hour % 12;
	FNumberFormattingOptions TwoDigits;
	TwoDigits.SetMinimumIntegralDigits(2);
	TimeText->SetText(FText::Format(LOCTEXT("ClockTime", "{0}:{1} {2}"), FText::AsNumber(HourOnClock),
		FText::AsNumber(ClockState.MinuteOfDay % 60, &TwoDigits), Hour < 12 ? LOCTEXT("AM", "AM") : LOCTEXT("PM", "PM")));

	FText PhaseLabel;
	FLinearColor PhaseColor(.85f, .91f, .93f);
	switch (ClockState.Phase)
	{
	case EShopClockPhase::Preparing:
		PhaseLabel = ClockState.bIsDemo ? LOCTEXT("Preparing", "Opens at 9 AM")
			: LOCTEXT("ClosedUntilOpening", "Closed · Opens at 9 AM");
		break;
	case EShopClockPhase::Open:
		PhaseLabel = LOCTEXT("Open", "Shop open");
		break;
	case EShopClockPhase::Rush:
		PhaseLabel = LOCTEXT("Rush", "Noon rush");
		PhaseColor = FLinearColor(.94f, .7f, .36f);
		break;
	case EShopClockPhase::Closed:
		PhaseLabel = ClockState.bIsDemo ? LOCTEXT("ClosedDemo", "Closed · Next day soon")
			: LOCTEXT("Closed", "Shop closed");
		break;
	}
	PhaseText->SetText(PhaseLabel);
	PhaseText->SetColorAndOpacity(PhaseColor);
}

#undef LOCTEXT_NAMESPACE
