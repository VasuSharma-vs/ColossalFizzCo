#include "UI/FlowControllerPressureDial.h"
#include "UI/ShopTerminalWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HoseSocketComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Rendering/DrawElements.h"
#include "Shop/ColaFlowController.h"
#include "Styling/CoreStyle.h"
#include "UI/FlowControllerIcon.h"
#include "UI/SettingsControls.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	class SFlowPressureDial : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFlowPressureDial) {} SLATE_ARGUMENT(TWeakObjectPtr<UFlowControllerPressureDial>, Reading) SLATE_END_ARGS()
		void Construct(const FArguments& Args) { Reading = Args._Reading; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(45, 37); }

		virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
			FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const override
		{
			using namespace FlowControllerUI;
			const double Fraction = Reading.IsValid() ? Reading->GetFraction() : 0;
			const FLinearColor Accent = Fraction >= 1 ? Red : Fraction >= .8 ? Brass : Mint;
			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D Center(Size.X * .5, Size.Y * .59);
			const double Radius = FMath::Min(Size.X * .43, Size.Y * .50);
			const auto Point = [&](double Position, double Distance)
			{
				const double Angle = FMath::DegreesToRadians(145. + 250. * Position);
				return Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
			};
			const auto Line = [&](FVector2D A, FVector2D B, const FLinearColor& Color, float Width)
			{
				const TArray<FVector2D> Points = {A, B};
				FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Width);
			};
			for (int32 Index = 0; Index <= 12; ++Index)
			{
				const double Position = Index / 12.;
				Line(Point(Position, Radius), Point(Position, Radius - (Index % 3 == 0 ? 4.5 : 3.0)),
					Fraction > 0 && Position <= Fraction ? Accent : Muted * .7f, 1.5f);
			}
			Line(Center, Point(FMath::Min(Fraction, 1.), Radius - 7), Accent, 1.8f);
			Line(Center - FVector2D(1.7, 0), Center + FVector2D(1.7, 0), Accent, 2.f);
			return Layer + 1;
		}
	private:
		TWeakObjectPtr<UFlowControllerPressureDial> Reading;
	};

	void DescribeHud(UWidget& Widget, const FText& Description)
	{
		Widget.SetToolTipText(Description);
#if WITH_ACCESSIBILITY
		if (const auto SlateWidget = Widget.GetCachedWidget())
			SlateWidget->SetAccessibleBehavior(EAccessibleBehavior::Custom, Description);
#endif
	}

	FText PressureReading(float Value) { return FText::FromString(FString::Printf(TEXT("%.2f"), Value)); }
	FText RateReading(float Value) { return FText::FromString(FString::Printf(TEXT("%.2f/s"), Value)); }
}

void UFlowControllerPressureDial::SetReading(float Value, float Limit)
{
	const float Next = FMath::Max(0.f, Value) / FMath::Max(.01f, Limit);
	if (Fraction == Next) return;
	Fraction = Next;
	if (const auto Widget = GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

TSharedRef<SWidget> UFlowControllerPressureDial::RebuildWidget()
{
	return SNew(SFlowPressureDial).Reading(this);
}

void UShopTerminalWidget::BuildFlowControllerHud(UCanvasPanel* Canvas)
{
	using namespace FlowControllerUI;
	const auto Label = [&](const FText& Value, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		auto* LabelWidget = WidgetTree->ConstructWidget<UTextBlock>();
		LabelWidget->SetText(Value);
		LabelWidget->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		LabelWidget->SetColorAndOpacity(Color);
		LabelWidget->SetClipping(EWidgetClipping::ClipToBounds);
		LabelWidget->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		return LabelWidget;
	};
	const auto Icon = [&](EFlowControllerIcon Glyph, const FLinearColor& Color, float Size)
	{
		auto* IconWidget = WidgetTree->ConstructWidget<UFlowControllerIcon>();
		IconWidget->SetIcon(Glyph);
		IconWidget->SetTint(Color);
		IconWidget->SetIconSize(FVector2D(Size));
		return IconWidget;
	};
	const auto Border = [&](const FLinearColor& Color, float Radius, const FMargin& InnerPadding)
	{
		auto* BorderWidget = WidgetTree->ConstructWidget<UBorder>();
		BorderWidget->SetBrush(FSlateRoundedBoxBrush(Color, Radius, Edge, 1.f));
		BorderWidget->SetPadding(InnerPadding);
		return BorderWidget;
	};

	auto* Card = Border(Panel, 12, FMargin(12));
	Card->SetRenderTransformPivot(FVector2D(1, 0));
	CornerPanels.Add(Card);
	auto* CanvasSlot = Canvas->AddChildToCanvas(Card);
	CanvasSlot->SetAnchors(FAnchors(1, 0));
	CanvasSlot->SetAlignment(FVector2D(1, 0));
	CanvasSlot->SetPosition(FVector2D(-16, 16));
	CanvasSlot->SetAutoSize(true);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(302);
	Card->SetContent(Size);
	auto* Stack = WidgetTree->ConstructWidget<UVerticalBox>();
	Size->SetContent(Stack);

	auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Stack->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 9));
	auto* Identity = Border(Inset, 7, FMargin(6));
	Identity->SetContent(Icon(EFlowControllerIcon::Controller, Brass, 23));
	Header->AddChildToHorizontalBox(Identity)->SetVerticalAlignment(VAlign_Center);
	auto* Caption = WidgetTree->ConstructWidget<UVerticalBox>();
	auto* CaptionSlot = Header->AddChildToHorizontalBox(Caption);
	CaptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CaptionSlot->SetPadding(FMargin(8, 0, 8, 0));
	CaptionSlot->SetVerticalAlignment(VAlign_Center);
	Caption->AddChildToVerticalBox(Label(NSLOCTEXT("FlowHud", "Title", "FLOW CONTROLLER"), 8, Brass, true));
	FlowHudFlavor = Label(FText::GetEmpty(), 13, Text, true);
	Caption->AddChildToVerticalBox(FlowHudFlavor)->SetPadding(FMargin(0, 1, 0, 0));

	const auto Dot = [&](UHorizontalBox* Row, float Radius, float Spacing)
	{
		auto* Pip = WidgetTree->ConstructWidget<UBorder>();
		Pip->SetBrush(FSlateRoundedBoxBrush(Inset, Radius, Edge, 1.f));
		Pip->SetPadding(FMargin(Radius));
		auto* DotSlot = Row->AddChildToHorizontalBox(Pip);
		DotSlot->SetVerticalAlignment(VAlign_Center);
		DotSlot->SetPadding(FMargin(0, 0, Spacing, 0));
		return Pip;
	};
	auto* Mode = WidgetTree->ConstructWidget<UVerticalBox>();
	Header->AddChildToHorizontalBox(Mode)->SetVerticalAlignment(VAlign_Center);
	FlowHudLeverIcon = Icon(EFlowControllerIcon::Hold, Brass, 20);
	Mode->AddChildToVerticalBox(FlowHudLeverIcon)->SetHorizontalAlignment(HAlign_Center);
	auto* Rank = WidgetTree->ConstructWidget<UHorizontalBox>();
	Mode->AddChildToVerticalBox(Rank)->SetPadding(FMargin(0, 3, 0, 0));
	FlowHudLeverPips.Reset();
	for (int32 Index = 0; Index < AColaFlowController::MaxLeverLevel - 1; ++Index)
		FlowHudLeverPips.Add(Dot(Rank, 2.5f, Index == AColaFlowController::MaxLeverLevel - 2 ? 0.f : 2.f));
	FlowHudStatusIcon = Icon(EFlowControllerIcon::Check, Mint, 17);
	auto* Status = Border(Inset, 6, FMargin(5));
	Status->SetContent(FlowHudStatusIcon);
	auto* StatusSlot = Header->AddChildToHorizontalBox(Status);
	StatusSlot->SetVerticalAlignment(VAlign_Center);
	StatusSlot->SetPadding(FMargin(8, 0, 0, 0));

	auto* Telemetry = WidgetTree->ConstructWidget<UHorizontalBox>();
	Stack->AddChildToVerticalBox(Telemetry);
	const auto Lane = [&](bool bInlet, TObjectPtr<UFlowControllerPressureDial>& Dial, TObjectPtr<UProgressBar>& Meter,
		TObjectPtr<UTextBlock>& Pressure, TObjectPtr<UTextBlock>& Flow)
	{
		const FLinearColor Accent = bInlet ? Blue : Mint;
		auto* Tile = Border(Surface, 8, FMargin(8, 6));
		auto* TileSlot = Telemetry->AddChildToHorizontalBox(Tile);
		TileSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bInlet) TileSlot->SetPadding(FMargin(0, 0, 6, 0));
		auto* Data = WidgetTree->ConstructWidget<UVerticalBox>();
		Tile->SetContent(Data);
		auto* Tag = WidgetTree->ConstructWidget<UHorizontalBox>();
		Data->AddChildToVerticalBox(Tag);
		Tag->AddChildToHorizontalBox(Icon(bInlet ? EFlowControllerIcon::Inlet : EFlowControllerIcon::Outlet, Accent, 13))->SetVerticalAlignment(VAlign_Center);
		auto* TagSlot = Tag->AddChildToHorizontalBox(Label(bInlet ? NSLOCTEXT("FlowHud", "IN", "IN") : NSLOCTEXT("FlowHud", "OUT", "OUT"), 8, Accent, true));
		TagSlot->SetPadding(FMargin(4, 0, 0, 0));
		TagSlot->SetVerticalAlignment(VAlign_Center);
		auto* PressureLine = WidgetTree->ConstructWidget<UHorizontalBox>();
		Data->AddChildToVerticalBox(PressureLine);
		Dial = WidgetTree->ConstructWidget<UFlowControllerPressureDial>();
		PressureLine->AddChildToHorizontalBox(Dial.Get());
		Pressure = Label(FText::GetEmpty(), 23, Text, true);
		auto* PressureSlot = PressureLine->AddChildToHorizontalBox(Pressure.Get());
		PressureSlot->SetVerticalAlignment(VAlign_Center);
		PressureSlot->SetPadding(FMargin(4, 0, 0, 0));
		auto* FlowLine = WidgetTree->ConstructWidget<UHorizontalBox>();
		Data->AddChildToVerticalBox(FlowLine)->SetPadding(FMargin(0, 2, 0, 0));
		FlowLine->AddChildToHorizontalBox(Icon(EFlowControllerIcon::Flow, Accent, 12))->SetVerticalAlignment(VAlign_Center);
		auto* Rail = WidgetTree->ConstructWidget<USizeBox>();
		Rail->SetHeightOverride(4);
		Meter = WidgetTree->ConstructWidget<UProgressBar>();
		FProgressBarStyle Style;
		Style.SetBackgroundImage(FSlateRoundedBoxBrush(Inset, 2.f));
		Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.f));
		Meter->SetWidgetStyle(Style);
		Meter->SetFillColorAndOpacity(Accent);
		Rail->SetContent(Meter.Get());
		auto* RailSlot = FlowLine->AddChildToHorizontalBox(Rail);
		RailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RailSlot->SetVerticalAlignment(VAlign_Center);
		RailSlot->SetPadding(FMargin(4, 0, 5, 0));
		Flow = Label(FText::GetEmpty(), 10, Accent, true);
		FlowLine->AddChildToHorizontalBox(Flow.Get())->SetVerticalAlignment(VAlign_Center);
	};
	Lane(true, FlowHudInletDial, FlowHudInletMeter, FlowHudInPressure, FlowHudInFlow);
	Lane(false, FlowHudOutletDial, FlowHudOutletMeter, FlowHudOutPressure, FlowHudOutFlow);

	auto* Devices = Border(Inset, 7, FMargin(7, 6));
	Stack->AddChildToVerticalBox(Devices)->SetPadding(FMargin(0, 7));
	auto* DeviceRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Devices->SetContent(DeviceRow);
	const auto Ports = [&](bool bInlet, int32 Count, TArray<TObjectPtr<UBorder>>& Pips)
	{
		auto* PortRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		DeviceRow->AddChildToHorizontalBox(PortRow)->SetPadding(FMargin(0, 0, 8, 0));
		auto* PortIcon = Icon(bInlet ? EFlowControllerIcon::Inlet : EFlowControllerIcon::Outlet, bInlet ? Blue : Mint, 12);
		auto* PortIconSlot = PortRow->AddChildToHorizontalBox(PortIcon);
		PortIconSlot->SetPadding(FMargin(0, 0, 4, 0));
		PortIconSlot->SetVerticalAlignment(VAlign_Center);
		Pips.Reset();
		for (int32 Index = 0; Index < Count; ++Index) Pips.Add(Dot(PortRow, 3.2f, 2.5f));
	};
	Ports(true, AColaFlowController::MaxInletPorts, FlowHudInPorts);
	Ports(false, AColaFlowController::MaxOutletPorts, FlowHudOutPorts);
	const auto Stat = [&](EFlowControllerIcon Glyph, const FLinearColor& Accent, bool bFill)
	{
		auto* StatBox = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* StatSlot = DeviceRow->AddChildToHorizontalBox(StatBox);
		if (bFill) StatSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatSlot->SetHorizontalAlignment(HAlign_Right);
		StatBox->AddChildToHorizontalBox(Icon(Glyph, Accent, 12))->SetVerticalAlignment(VAlign_Center);
		auto* Readout = Label(FText::GetEmpty(), 9, Text, true);
		StatBox->AddChildToHorizontalBox(Readout)->SetPadding(FMargin(3, 0, bFill ? 7.f : 0.f, 0));
		return Readout;
	};
	FlowHudPerOutlet = Stat(EFlowControllerIcon::Link, Mint, true);
	FlowHudBottleCount = Stat(EFlowControllerIcon::Bottle, Brass, false);

	auto* Commands = WidgetTree->ConstructWidget<UHorizontalBox>();
	Stack->AddChildToVerticalBox(Commands);
	const auto Keycap = [&](EFlowControllerIcon Glyph, const FLinearColor& Accent, bool bHasGap, UFlowControllerIcon** OutIcon = nullptr)
	{
		auto* Chip = Border(Surface, 5, FMargin(5, 3));
		auto* ChipSlot = Commands->AddChildToHorizontalBox(Chip);
		ChipSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bHasGap) ChipSlot->SetPadding(FMargin(0, 0, 5, 0));
		auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Chip->SetHorizontalAlignment(HAlign_Center);
		Chip->SetContent(Row);
		auto* Action = Icon(Glyph, Accent, 14);
		if (OutIcon) *OutIcon = Action;
		Row->AddChildToHorizontalBox(Action)->SetVerticalAlignment(VAlign_Center);
		auto* Key = Label(FText::GetEmpty(), 9, Text, true);
		auto* KeySlot = Row->AddChildToHorizontalBox(Key);
		KeySlot->SetPadding(FMargin(5, 0, 0, 0));
		KeySlot->SetVerticalAlignment(VAlign_Center);
		return Key;
	};
	FlowHudLeverKey = Keycap(EFlowControllerIcon::Lever, Mint, true);
	FlowHudUpgradeKey = Keycap(EFlowControllerIcon::Upgrade, Brass, true);
	FlowHudMoveKey = Keycap(EFlowControllerIcon::Move, Blue, true);
	UFlowControllerIcon* PauseIcon = nullptr;
	FlowHudPauseKey = Keycap(EFlowControllerIcon::Pause, Muted, false, &PauseIcon);
	FlowHudPauseIcon = PauseIcon;
}

void UShopTerminalWidget::RefreshFlowController(const AColaFlowController& Flow)
{
	using namespace FlowControllerUI;
	if (!FlowHudFlavor) return;
	const auto* Manager = APropManager::Get(this);
	const auto* Content = Manager ? Manager->FindContentDefinition(Flow.SelectedContentID) : nullptr;
	FlowHudFlavor->SetText(Content ? Content->DisplayName : NSLOCTEXT("FlowHud", "NoSupply", "No supply"));
	DescribeHud(*FlowHudFlavor, FlowHudFlavor->GetText());

	const float IntakeLimit = Flow.GetInletPressureTolerance() * (1.f + Flow.GetIntakeSuctionStrength());
	float OutletLimit = Flow.FillerSockets.IsEmpty() ? IntakeLimit : Flow.FillerSockets[0]->MaxOuttakePressure;
	for (const UHoseSocketComponent* Socket : Flow.FillerSockets)
		OutletLimit = FMath::Min(OutletLimit, Socket->MaxOuttakePressure);
	const auto UpdateLane = [&](UFlowControllerPressureDial& Dial, UProgressBar& Meter,
		UTextBlock& PressureText, UTextBlock& RateText, float Pressure, float PressureLimit, float Rate, float Cap, bool bInlet)
	{
		Dial.SetReading(Pressure, PressureLimit);
		Meter.SetPercent(FMath::Clamp(Rate / FMath::Max(.01f, Cap), 0.f, 1.f));
		PressureText.SetText(PressureReading(Pressure));
		RateText.SetText(RateReading(Rate));
		DescribeHud(PressureText, FText::Format(bInlet ? NSLOCTEXT("FlowHud", "InPressure", "Inlet pressure {0}")
			: NSLOCTEXT("FlowHud", "OutPressure", "Pressure per outlet {0}"), PressureText.GetText()));
		DescribeHud(RateText, FText::Format(bInlet ? NSLOCTEXT("FlowHud", "InRate", "Inlet flow {0} portions per second")
			: NSLOCTEXT("FlowHud", "OutRate", "Total outlet flow {0} portions per second"), PressureReading(Rate)));
	};
	UpdateLane(*FlowHudInletDial, *FlowHudInletMeter, *FlowHudInPressure, *FlowHudInFlow,
		Flow.InletSupplyPressure, IntakeLimit, Flow.InletPortionsPerSecond, Flow.GetIntakeRate(), true);
	UpdateLane(*FlowHudOutletDial, *FlowHudOutletMeter, *FlowHudOutPressure, *FlowHudOutFlow,
		Flow.PressurePerOutlet, OutletLimit, Flow.OutletPortionsPerSecond, Flow.GetOutputRate(), false);
	FlowHudPerOutlet->SetText(RateReading(Flow.PortionsPerSecondPerFiller));
	DescribeHud(*FlowHudPerOutlet, FText::Format(NSLOCTEXT("FlowHud", "PerOutlet", "Allocated per outlet {0} portions per second"), PressureReading(Flow.PortionsPerSecondPerFiller)));
	FlowHudBottleCount->SetText(FText::AsNumber(Flow.ActiveFillerCount));
	DescribeHud(*FlowHudBottleCount, FText::Format(NSLOCTEXT("FlowHud", "Bottles", "Bottles held {0}"), FlowHudBottleCount->GetText()));

	const auto RefreshPorts = [&](const TArray<TObjectPtr<UBorder>>& Pips, const TArray<TObjectPtr<UHoseSocketComponent>>& Sockets, bool bInlet)
	{
		for (int32 Index = 0; Index < Pips.Num(); ++Index)
		{
			auto* Pip = Pips[Index].Get();
			const auto* Socket = Sockets.IsValidIndex(Index) ? Sockets[Index].Get() : nullptr;
			Pip->SetVisibility(Socket ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			if (!Socket) continue;
			const bool bSelected = bInlet && Index == Flow.SelectedInputIndex;
			const bool bPlugged = Socket->ConnectedHose != nullptr;
			const bool bOpen = bPlugged && (bInlet ? Socket->bRequestsFlow : Socket->bSupplyEnabled);
			const FLinearColor PortColor = Socket->bIsBroken ? Red : bInlet ? Blue : Mint;
			const FLinearColor Fill = Socket->bIsBroken || bOpen ? PortColor : Inset;
			const FLinearColor Outline = bSelected ? Brass : bPlugged ? PortColor : Muted;
			Pip->SetBrush(FSlateRoundedBoxBrush(Fill, 3.2f, Outline, bSelected ? 1.7f : 1.f));
			const auto Port = bInlet ? NSLOCTEXT("FlowHud", "InletPort", "Inlet {0}") : NSLOCTEXT("FlowHud", "OutletPort", "Outlet {0}");
			const auto State = Socket->bIsBroken ? NSLOCTEXT("FlowHud", "Broken", "broken") : bPlugged
				? (bOpen ? NSLOCTEXT("FlowHud", "Open", "connected, open") : NSLOCTEXT("FlowHud", "Connected", "connected"))
				: NSLOCTEXT("FlowHud", "Empty", "empty");
			DescribeHud(*Pip, FText::Format(bSelected ? NSLOCTEXT("FlowHud", "SelectedPort", "{0}, {1}, selected")
				: NSLOCTEXT("FlowHud", "Port", "{0}, {1}"), FText::Format(Port, FText::AsNumber(Index + 1)), State));
		}
	};
	RefreshPorts(FlowHudInPorts, Flow.SelectorSockets, true);
	RefreshPorts(FlowHudOutPorts, Flow.FillerSockets, false);

	const EFlowControllerIcon Modes[] = { EFlowControllerIcon::Hold, EFlowControllerIcon::Toggle, EFlowControllerIcon::AutoStop, EFlowControllerIcon::Automatic };
	const FText Names[] = { NSLOCTEXT("FlowHud", "Hold", "Hold to fill"), NSLOCTEXT("FlowHud", "Toggle", "Toggle flow"),
		NSLOCTEXT("FlowHud", "AutoStop", "Stop when any bottle fills"), NSLOCTEXT("FlowHud", "Automatic", "Refill replacements automatically") };
	const int32 Mode = FMath::Clamp(Flow.LeverLevel - 1, 0, static_cast<int32>(UE_ARRAY_COUNT(Modes)) - 1);
	FlowHudLeverIcon->SetIcon(Modes[Mode]);
	DescribeHud(*FlowHudLeverIcon, FText::Format(NSLOCTEXT("FlowHud", "LeverRank", "Lever rank {0} of 3. {1}."), FText::AsNumber(Mode), Names[Mode]));
	for (int32 Index = 0; Index < FlowHudLeverPips.Num(); ++Index)
		FlowHudLeverPips[Index]->SetBrush(FSlateRoundedBoxBrush(Index < Mode ? Brass : Edge, 2.5f));

	const auto* PC = GetOwningPlayer();
	const bool bLocalOperator = PC && Flow.IsLeverHeldBy(PC->GetPawn());
	if (!Flow.bLeverHeld)
	{
		FlowHudStatusIcon->SetIcon(EFlowControllerIcon::Check);
		FlowHudStatusIcon->SetTint(Mint);
		DescribeHud(*FlowHudStatusIcon, NSLOCTEXT("FlowHud", "Ready", "Lever ready"));
	}
	else if (!bLocalOperator)
	{
		FlowHudStatusIcon->SetIcon(EFlowControllerIcon::Lock);
		FlowHudStatusIcon->SetTint(Brass);
		DescribeHud(*FlowHudStatusIcon, NSLOCTEXT("FlowHud", "InUse", "Lever in use"));
	}
	else if (Flow.bFlowPaused)
	{
		FlowHudStatusIcon->SetIcon(Flow.bBatchSafeToRelease ? EFlowControllerIcon::Pause : EFlowControllerIcon::Warning);
		FlowHudStatusIcon->SetTint(Brass);
		DescribeHud(*FlowHudStatusIcon, Flow.bBatchSafeToRelease ? NSLOCTEXT("FlowHud", "Paused", "Paused, safe to release")
			: NSLOCTEXT("FlowHud", "High", "Paused, pressure high. Keep the lever pulled."));
	}
	else
	{
		FlowHudStatusIcon->SetIcon(Flow.ActiveFillerCount > 0 ? EFlowControllerIcon::Play : EFlowControllerIcon::Clock);
		FlowHudStatusIcon->SetTint(Flow.ActiveFillerCount > 0 ? Mint : Brass);
		DescribeHud(*FlowHudStatusIcon, Flow.ActiveFillerCount > 0 ? NSLOCTEXT("FlowHud", "Filling", "Filling")
			: NSLOCTEXT("FlowHud", "NoBottles", "Lever pulled, no bottles ready"));
	}
	const FKey Interact = SettingsControls::InteractKey(GetOwningLocalPlayer());
	const FText Key = Interact.GetDisplayName(false);
	const FText LongKey = Interact.GetDisplayName();
	FlowHudLeverKey->SetText(Key);
	FlowHudLeverKey->SetRenderOpacity(Flow.bLeverHeld && !bLocalOperator ? .42f : 1.f);
	DescribeHud(*FlowHudLeverKey, FText::Format(Flow.UsesLatchedLever() ? NSLOCTEXT("FlowHud", "PressLever", "Press {0} on the lever")
		: NSLOCTEXT("FlowHud", "HoldLever", "Hold {0} on the lever"), LongKey));
	FlowHudUpgradeKey->SetText(Key);
	DescribeHud(*FlowHudUpgradeKey, FText::Format(NSLOCTEXT("FlowHud", "UpgradeHelp", "Press {0} on the yellow cylinder for upgrades"), LongKey));
	FlowHudMoveKey->SetText(EKeys::M.GetDisplayName(false));
	DescribeHud(*FlowHudMoveKey, NSLOCTEXT("FlowHud", "MoveHelp", "Hold M to move the controller"));
	FlowHudPauseKey->SetText(Flow.bFlowPaused ? EKeys::V.GetDisplayName(false) : EKeys::X.GetDisplayName(false));
	FlowHudPauseKey->SetRenderOpacity(bLocalOperator ? 1.f : .42f);
	FlowHudPauseIcon->SetIcon(Flow.bFlowPaused ? EFlowControllerIcon::Play : EFlowControllerIcon::Pause);
	FlowHudPauseIcon->SetTint(bLocalOperator ? Brass : Muted);
	DescribeHud(*FlowHudPauseKey, Flow.bFlowPaused ? NSLOCTEXT("FlowHud", "ResumeHelp", "Press V to resume flow")
		: NSLOCTEXT("FlowHud", "PauseHelp", "Press X to stop flow"));
}
