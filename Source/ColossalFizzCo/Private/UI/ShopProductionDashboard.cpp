#include "UI/ShopTerminalWidget.h"
#include "UI/ManualFillingPressureGauge.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"

void UShopTerminalWidget::BuildProductionDashboard(UCanvasPanel* Canvas)
{
	if (bFlowControllerDisplay)
	{
		BuildFlowControllerHud(Canvas);
		return;
	}
	const FLinearColor DashboardCream(.91f,.93f,.87f), DashboardMuted(.48f,.64f,.66f), Mint(.23f,.85f,.65f), Gold(.94f,.7f,.36f);
	auto Label=[&](UVerticalBox* Parent,const FString& Value,int32 Size,FLinearColor Color,bool Bold=false)
	{
		auto* Text=WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Value)); Text->SetAutoWrapText(true);
		Text->SetFont(FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular",Size)); Text->SetColorAndOpacity(Color);
		Parent->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,3)); return Text;
	};
	auto Card=[&](FVector2D Anchor,FVector2D Offset,FVector2D Size)
	{
		auto* Border=WidgetTree->ConstructWidget<UBorder>(); Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f,.065f,.08f,.95f),12.f));
		Border->SetPadding(FMargin(16)); Border->SetRenderTransformPivot(Anchor); CornerPanels.Add(Border);
		auto* Slot=Canvas->AddChildToCanvas(Border); Slot->SetAnchors(FAnchors(Anchor.X,Anchor.Y)); Slot->SetAlignment(Anchor); Slot->SetPosition(Offset); Slot->SetSize(Size);
		auto* Content=WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Content); return Content;
	};
	const auto* Production=Station.IsValid() ? Station->FindComponentByClass<UShopProductionStationComponent>() : nullptr;
	const bool Manual=Production && Production->bManualLeverMode;
	if (!Manual)
	{
		auto* Store=Card(FVector2D(1,0),FVector2D(-18,18),FVector2D(340,650));
		Label(Store,TEXT("COLOSSAL FIZZ CO."),10,Gold,true); Label(Store,TEXT("Supply depot"),22,DashboardCream,true);
		Summary=Label(Store,TEXT("Shop bank"),11,DashboardMuted);
		auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Store->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Body=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Body); ProductionSidebar=Body;
		auto* CloseButton=WidgetTree->ConstructWidget<UShopTerminalButton>(); CloseButton->Configure(this,TEXT("ESC   Close depot")); CloseButton->bClose=true; Store->AddChild(CloseButton);
		ResultText=Label(Store,TEXT("Purchases arrive at the delivery point."),10,DashboardMuted); return;
	}
	const auto* Table=Cast<AManualColaFillingMachine>(Production->GetOwner());
	const bool bSplitTable=bWorldFillingDisplay && Table && Table->bUseFlowController;
	auto* Instrument=Card(FVector2D(0,0),FVector2D(18,18),bWorldFillingDisplay ? FVector2D(304,bSplitTable ? 268 : 248) : FVector2D(330,310));
	Label(Instrument,bWorldFillingDisplay ? TEXT("BOTTLING") : TEXT("COLOSSAL FIZZ CO.  /  MANUAL BOTTLING"),10,Gold,true);
	ProductionStatus=Label(Instrument,TEXT("Ready for a bottle"),18,DashboardCream,true);
	if (!bWorldFillingDisplay) Summary=Label(Instrument,TEXT("Connect a physical supply hose"),11,DashboardMuted);
	auto* Readings=WidgetTree->ConstructWidget<UHorizontalBox>(); Instrument->AddChild(Readings);
	PressureGauge=WidgetTree->ConstructWidget<UManualFillingPressureGauge>();
	auto* GaugeSize=WidgetTree->ConstructWidget<USizeBox>(); GaugeSize->SetWidthOverride(bWorldFillingDisplay ? 129 : 170); GaugeSize->SetHeightOverride(bWorldFillingDisplay ? 124 : 145); GaugeSize->SetContent(PressureGauge);
	Readings->AddChild(GaugeSize);
	auto* Metrics=WidgetTree->ConstructWidget<UVerticalBox>(); auto* MetricSlot=Readings->AddChildToHorizontalBox(Metrics);
	MetricSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); MetricSlot->SetVerticalAlignment(VAlign_Center);
	if (bWorldFillingDisplay) MetricSlot->SetPadding(FMargin(9,0,0,0));
	FillReadout=Label(Metrics,TEXT("0% FULL"),14,DashboardCream,true);
	auto Bar=[&](UProgressBar*& Result,FLinearColor Color)
	{
		auto* Height=WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(6);
		Metrics->AddChildToVerticalBox(Height)->SetPadding(bWorldFillingDisplay ? FMargin(0,3,0,7) : FMargin(0,7,0,12));
		Result=WidgetTree->ConstructWidget<UProgressBar>(); Result->SetFillColorAndOpacity(Color); Height->SetContent(Result);
		FProgressBarStyle Style; Style.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.01f,.03f,.04f),3.f));
		Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,3.f)); Result->SetWidgetStyle(Style);
	};
	UProgressBar* Fill=nullptr; Bar(Fill,Mint); FillProgress=Fill;
	if (bWorldFillingDisplay)
	{
		CapTightnessReadout=Label(Metrics,TEXT("CAP —"),11,DashboardCream,true);
		UProgressBar* Cap=nullptr; Bar(Cap,FLinearColor(.48f,.76f,.97f)); CapTightnessProgress=Cap;
	}
	SealReadout=Label(Metrics,TEXT("SEAL INACTIVE"),9,DashboardMuted,true);
	UProgressBar* Seal=nullptr; Bar(Seal,Gold); SealProgress=Seal;
	ProductionHint=Label(Instrument,FString(),11,DashboardMuted);

	if (bSplitTable)
	{
		SupplyTrayReadout=Label(Instrument,FString(),10,DashboardMuted);
		return;
	}
	if (bWorldFillingDisplay)
	{
		auto* Supply=Card(FVector2D(1,0),FVector2D(-18,18),FVector2D(268,182));
		Label(Supply,TEXT("HOSE INPUT"),10,Gold,true);
		Summary=Label(Supply,TEXT("Waiting for supply"),13,DashboardCream,true);
		HoseReadout=Label(Supply,FString(),11,Mint,true);
		Label(Supply,TEXT("E  Turn dial with lever released"),10,DashboardMuted);
		SupplyTrayReadout=Label(Supply,TEXT("Caps 0 / 4    Seals 0 / 4"),11,DashboardMuted);
		return;
	}

	auto* Controls=Card(FVector2D(0,1),FVector2D(18,-18),FVector2D(330,270));
	Label(Controls,TEXT("BOTTLE CONTROLS"),10,Gold,true);
	auto* WorkspaceScroll=WidgetTree->ConstructWidget<UScrollBox>(); Controls->AddChildToVerticalBox(WorkspaceScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Body=WidgetTree->ConstructWidget<UVerticalBox>(); WorkspaceScroll->AddChild(Body);
	Label(Controls,TEXT("F Hold   X Stop   V Resume   R Cap"),10,DashboardMuted);

	auto* Ports=Card(FVector2D(1,0),FVector2D(-18,18),FVector2D(300,400));
	Label(Ports,TEXT("CONTENT & HOSES"),10,Gold,true);
	auto* PortScroll=WidgetTree->ConstructWidget<UScrollBox>(); Ports->AddChildToVerticalBox(PortScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ProductionSidebar=WidgetTree->ConstructWidget<UVerticalBox>(); PortScroll->AddChild(ProductionSidebar);
	auto* CloseButton=WidgetTree->ConstructWidget<UShopTerminalButton>(); CloseButton->Configure(this,TEXT("ESC   Close controls")); CloseButton->bClose=true; Ports->AddChild(CloseButton);

	auto* Trays=Card(FVector2D(1,1),FVector2D(-18,-18),FVector2D(300,166));
	Label(Trays,TEXT("PHYSICAL SUPPLY TRAYS"),10,Gold,true);
	SupplyTrayReadout=Label(Trays,TEXT("Caps 0 / 4\nSeals 0 / 4"),16,DashboardCream,true);
	ResultText=Label(Trays,TEXT("Drop supplies into the trays.\nBuy refills at the supply depot."),10,DashboardMuted);
}

UTextBlock* UShopTerminalWidget::AddSection(const FString& Title,const FString& Description,UVerticalBox* Target)
{
	if (!Target) Target=Body;
	auto* TitleText=WidgetTree->ConstructWidget<UTextBlock>(); TitleText->SetText(FText::FromString(Title));
	TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold",11)); TitleText->SetColorAndOpacity(FLinearColor(.94f,.7f,.36f));
	Target->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0,10,0,4));
	auto* Text=WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Description)); Text->SetAutoWrapText(true);
	Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",11)); Text->SetColorAndOpacity(FLinearColor(.55f,.69f,.71f));
	Target->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,0,0,6)); return Text;
}

UShopTerminalButton* UShopTerminalWidget::AddProductionAction(const FString& Label,EShopProductionAction Action,int32 BottleID,int32 OtherID,UVerticalBox* Target)
{
	auto* Button=WidgetTree->ConstructWidget<UShopTerminalProductionButton>(); Button->Configure(this,Label); Button->bProduction=true;
	Button->ProductionAction=Action; Button->ISID=BottleID; Button->OtherISID=OtherID;
	if (Station.IsValid()) if (auto* Production=Station->FindComponentByClass<UShopProductionStationComponent>()) Button->SessionID=Production->View.SessionID;
	(Target ? Target : Body.Get())->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3)); return Button;
}

