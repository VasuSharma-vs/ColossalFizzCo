#include "UI/ShopTerminalWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ColaFlowController.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Styling/CoreStyle.h"
#include "UI/FlowControllerIcon.h"
#include "UI/SettingsControls.h"
#include "UI/ShopMoneyText.h"

namespace
{
	struct FFlowUpgradeBranch
	{
		EFlowControllerUpgradeType Type;
		EFlowControllerIcon Icon;
		const TCHAR* WidgetName;
		const TCHAR* Label;
		const TCHAR* Name;
	};

	constexpr FFlowUpgradeBranch FlowUpgradeBranches[] = {
		{ EFlowControllerUpgradeType::InputPorts, EFlowControllerIcon::Inlet, TEXT("FlowUpgradeTabIn"), TEXT("Inlets"), TEXT("Supply Manifold") },
		{ EFlowControllerUpgradeType::OutputPorts, EFlowControllerIcon::Outlet, TEXT("FlowUpgradeTabOut"), TEXT("Outlets"), TEXT("Distribution Manifold") },
		{ EFlowControllerUpgradeType::Flow, EFlowControllerIcon::Flow, TEXT("FlowUpgradeTabFlow"), TEXT("Flow · p/s"), TEXT("Flow Chamber") },
		{ EFlowControllerUpgradeType::Suction, EFlowControllerIcon::Suction, TEXT("FlowUpgradeTabSuction"), TEXT("Suction"), TEXT("Intake Assist") },
		{ EFlowControllerUpgradeType::Lever, EFlowControllerIcon::Lever, TEXT("FlowUpgradeTabLever"), TEXT("Lever"), TEXT("Lever Mechanism") },
	};
	struct FFillingUpgradeBranch
	{
		EFillingMachineUpgradeType Type;
		const TCHAR* Label;
		const TCHAR* Name;
		const TCHAR* Unit;
		const TCHAR* Description;
	};

	constexpr FFillingUpgradeBranch FillingUpgradeBranches[] = {
		{ EFillingMachineUpgradeType::CapTray, TEXT("CAPS"), TEXT("Cap tray"), TEXT(" cap slots"), TEXT("Adds one cap to the tray's capacity.") },
		{ EFillingMachineUpgradeType::SealTray, TEXT("SEALS"), TEXT("Seal tray"), TEXT(" seal slots"), TEXT("Adds one seal to the tray's capacity.") },
		{ EFillingMachineUpgradeType::PressureRegulator, TEXT("PRESSURE"), TEXT("Pressure regulator"), TEXT(" pressure reduction"), TEXT("Reduces pressure in the bottle during filling by 0.05 per rank.") },
		{ EFillingMachineUpgradeType::TemperatureRegulator, TEXT("COOLING"), TEXT("Temperature regulator"), TEXT(" °C cooler"), TEXT("Cools incoming cola by 0.02 °C per rank while filling.") },
	};
	struct FPlayerUpgradeBranch
	{
		EPlayerUpgradeType Type;
		const TCHAR* Label;
		const TCHAR* Name;
		const TCHAR* Unit;
	};

	constexpr FPlayerUpgradeBranch PlayerUpgradeBranches[] = {
		{ EPlayerUpgradeType::EmptyBottleHandling, TEXT("EMPTY"), TEXT("Empty Bottle Handling"), TEXT(" empty bottles") },
		{ EPlayerUpgradeType::CapHandling, TEXT("CAPS"), TEXT("Cap Handling"), TEXT(" caps") },
		{ EPlayerUpgradeType::SealHandling, TEXT("SEALS"), TEXT("Seal Handling"), TEXT(" seals") },
		{ EPlayerUpgradeType::HandStability, TEXT("STABILITY"), TEXT("Hand Stability"), TEXT(" less movement shake") },
	};
	constexpr const TCHAR* LeverModeLabels[] = { TEXT("HOLD"), TEXT("TOGGLE"), TEXT("AUTO-STOP"), TEXT("AUTO") };
	constexpr EFlowControllerIcon LeverModeIcons[] = {
		EFlowControllerIcon::Hold, EFlowControllerIcon::Toggle, EFlowControllerIcon::AutoStop, EFlowControllerIcon::Automatic
	};
	constexpr const TCHAR* LeverModeDescriptions[] = {
		TEXT("Hold {key} to fill. Release {key} to stop."),
		TEXT("Press {key} once to fill and again to stop."),
		TEXT("The first full bottle stops the whole controller. Press {key} to start again."),
		TEXT("Each full bottle closes its own outlet. Place a ready replacement bottle to reopen it."),
	};
	const FLinearColor UpgradeGold(.94f,.70f,.36f), UpgradeCream(.93f,.94f,.88f), UpgradeMint(.58f,.94f,.83f), UpgradeMuted(.65f,.76f,.77f);

	const FFlowUpgradeBranch& GetFlowUpgradeBranch(EFlowControllerUpgradeType Type)
	{
		return FlowUpgradeBranches[static_cast<uint8>(Type)];
	}

	const FFillingUpgradeBranch& GetFillingUpgradeBranch(EFillingMachineUpgradeType Type)
	{
		return FillingUpgradeBranches[static_cast<uint8>(Type)];
	}

	FString GetPlayerUpgradeValueText(EPlayerUpgradeType Type, float Value)
	{
		if (Type == EPlayerUpgradeType::HandStability)
			return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.f * (1.f - Value)));
		return FString::FromInt(FMath::RoundToInt(Value));
	}

	FString GetFillingUpgradeValueText(EFillingMachineUpgradeType Type,float Value)
	{
		switch (Type)
		{
		case EFillingMachineUpgradeType::CapTray:
		case EFillingMachineUpgradeType::SealTray: return FString::FromInt(FMath::RoundToInt(Value));
		case EFillingMachineUpgradeType::PressureRegulator:
		case EFillingMachineUpgradeType::TemperatureRegulator: return FString::Printf(TEXT("%.2f"),Value);
		}
		return FString();
	}

	FString GetFlowUpgradeValueText(const AColaFlowController& Flow, EFlowControllerUpgradeType Type, int32 Rank)
	{
		const float Value=Flow.GetUpgradeValue(Type,Rank);
		FNumberFormattingOptions Format;
		Format.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(2);
		switch (Type)
		{
		case EFlowControllerUpgradeType::InputPorts:
		case EFlowControllerUpgradeType::OutputPorts: return FString::FromInt(FMath::RoundToInt(Value));
		case EFlowControllerUpgradeType::Flow:
		{
			const float Output=Flow.GetOutputRateForFlowRank(Rank);
			const FString Intake=FText::AsNumber(Value,&Format).ToString();
			return FMath::IsNearlyEqual(Value,Output) ? Intake : Intake+TEXT(" / ")+FText::AsNumber(Output,&Format).ToString();
		}
		case EFlowControllerUpgradeType::Suction: return FText::AsNumber(Value,&Format).ToString();
		case EFlowControllerUpgradeType::Lever: return LeverModeLabels[FMath::Clamp(Rank,0,3)];
		}
		return FString();
	}

	UFlowControllerIcon* MakeFlowUpgradeIcon(UWidgetTree& Tree,EFlowControllerIcon Icon,const FLinearColor& Tint,float Size)
	{
		auto* Widget=Tree.ConstructWidget<UFlowControllerIcon>();
		Widget->Configure(Icon,Tint,FVector2D(Size));
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Widget;
	}

	FButtonStyle MakeFlowUpgradeButtonStyle(const FLinearColor& Normal,const FLinearColor& Outline,const FMargin& Padding)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Normal,8.f,Outline,1.f));
		Style.SetHovered(FSlateRoundedBoxBrush(Normal*1.25f,8.f,FlowControllerUI::Brass,1.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FlowControllerUI::Inset,8.f,FlowControllerUI::Mint,1.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(FlowControllerUI::Inset,8.f,FlowControllerUI::Edge,1.f));
		Style.SetNormalPadding(Padding);
		Style.SetPressedPadding(Padding);
		return Style;
	}

	void AddFlowUpgradeMeter(UWidgetTree& Tree,UVerticalBox& Graphic,EFlowControllerIcon Icon,float Value,float Maximum,const FLinearColor& Tint)
	{
		auto* Row=Tree.ConstructWidget<UHorizontalBox>();
		auto* RowSlot=Graphic.AddChildToVerticalBox(Row);
		RowSlot->SetHorizontalAlignment(HAlign_Center);
		RowSlot->SetPadding(FMargin(0.f,1.f));
		Row->AddChildToHorizontalBox(MakeFlowUpgradeIcon(Tree,Icon,Tint,14.f))->SetVerticalAlignment(VAlign_Center);
		auto* Size=Tree.ConstructWidget<USizeBox>();
		Size->SetWidthOverride(108.f);
		Size->SetHeightOverride(5.f);
		auto* SizeSlot=Row->AddChildToHorizontalBox(Size);
		SizeSlot->SetVerticalAlignment(VAlign_Center);
		SizeSlot->SetPadding(FMargin(6.f,0.f,0.f,0.f));
		auto* Meter=Tree.ConstructWidget<UProgressBar>();
		FProgressBarStyle Style;
		Style.SetBackgroundImage(FSlateRoundedBoxBrush(FlowControllerUI::Edge,2.5f));
		Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,2.5f));
		Meter->SetWidgetStyle(Style);
		Meter->SetFillColorAndOpacity(Tint);
		Meter->SetPercent(Maximum>0.f ? FMath::Clamp(Value/Maximum,0.f,1.f) : 0.f);
		Size->SetContent(Meter);
	}

	void BuildFlowUpgradeGraphic(UWidgetTree& Tree,UVerticalBox& Graphic,const AColaFlowController& Flow,
		EFlowControllerUpgradeType Type,int32 Rank,int32 MaxRank,const FLinearColor& Tint)
	{
		Graphic.ClearChildren();
		if (Type==EFlowControllerUpgradeType::Lever)
		{
			Graphic.AddChildToVerticalBox(MakeFlowUpgradeIcon(Tree,LeverModeIcons[FMath::Clamp(Rank,0,3)],Tint,31.f))
				->SetHorizontalAlignment(HAlign_Center);
			return;
		}
		const float Value=Flow.GetUpgradeValue(Type,Rank);
		const float Maximum=Flow.GetUpgradeValue(Type,MaxRank);
		if (Type==EFlowControllerUpgradeType::Flow)
		{
			AddFlowUpgradeMeter(Tree,Graphic,EFlowControllerIcon::Inlet,Value,Maximum,Tint);
			AddFlowUpgradeMeter(Tree,Graphic,EFlowControllerIcon::Outlet,Flow.GetOutputRateForFlowRank(Rank),Flow.GetOutputRateForFlowRank(MaxRank),Tint);
			return;
		}
		if (Type==EFlowControllerUpgradeType::Suction)
		{
			AddFlowUpgradeMeter(Tree,Graphic,EFlowControllerIcon::Suction,Value,Maximum,Tint);
			return;
		}
		const int32 Capacity=FMath::RoundToInt(Maximum);
		constexpr int32 PortsPerRow=6;
		for (int32 FirstPort=0; FirstPort<Capacity; FirstPort+=PortsPerRow)
		{
			auto* Row=Tree.ConstructWidget<UHorizontalBox>();
			auto* RowSlot=Graphic.AddChildToVerticalBox(Row);
			RowSlot->SetHorizontalAlignment(HAlign_Center);
			RowSlot->SetPadding(FMargin(0.f,2.f));
			for (int32 Index=FirstPort; Index<FMath::Min(FirstPort+PortsPerRow,Capacity); ++Index)
			{
				const bool bEnabled=Index<FMath::RoundToInt(Value);
				auto* Size=Tree.ConstructWidget<USizeBox>();
				Size->SetWidthOverride(17.f);
				Size->SetHeightOverride(11.f);
				Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(2.f,0.f));
				auto* Port=Tree.ConstructWidget<UBorder>();
				Port->SetBrush(FSlateRoundedBoxBrush(bEnabled ? Tint : FlowControllerUI::Inset,5.f,
					bEnabled ? Tint : FlowControllerUI::Edge,1.f));
				Size->SetContent(Port);
			}
		}
	}

	int32 GetSuctionReduction(const AColaFlowController& Flow,int32 Rank)
	{
		return FMath::RoundToInt(100.f*(1.f-1.f/(1.f+Flow.GetUpgradeValue(EFlowControllerUpgradeType::Suction,Rank))));
	}

	FString GetFlowUpgradeDescription(const AColaFlowController& Flow, EFlowControllerUpgradeType Type, int32 Rank, int32 NextRank, const ULocalPlayer* Player)
	{
		switch (Type)
		{
		case EFlowControllerUpgradeType::InputPorts:
			return TEXT("Connect more supply containers. The chamber still limits total intake.");
		case EFlowControllerUpgradeType::OutputPorts:
			return TEXT("Connect more filling stations. Open outlets share the controller's available flow.");
		case EFlowControllerUpgradeType::Flow:
			if (Rank==NextRank) return FString::Printf(TEXT("Inlet pressure tolerance is %.0f%% of base."),100.f*Flow.GetInletToleranceMultiplier(Rank));
			return FString::Printf(TEXT("Raises total intake and output. Inlet pressure tolerance %.0f%% → %.0f%% of base."),
				100.f*Flow.GetInletToleranceMultiplier(Rank),100.f*Flow.GetInletToleranceMultiplier(NextRank));
		case EFlowControllerUpgradeType::Suction:
			return FString::Printf(TEXT("Eases pressure on the connected inlet hose. Hose pressure load is reduced by %.0f%% at this rank."),
				100.f*(1.f-1.f/(1.f+Flow.GetUpgradeValue(Type,NextRank))));
		case EFlowControllerUpgradeType::Lever:
			return FString(LeverModeDescriptions[FMath::Clamp(NextRank,0,3)]).Replace(TEXT("{key}"),
				*SettingsControls::InteractKey(Player).GetDisplayName().ToString());
		}
		return FString();
	}

	void SetFlowUpgradeButtonText(UShopTerminalButton& Button,const FString& Label,const FLinearColor& Color)
	{
		if (auto* ButtonLabel=Cast<UTextBlock>(Button.GetContent()))
		{
			ButtonLabel->SetText(FText::FromString(Label));
			ButtonLabel->SetColorAndOpacity(Color);
		}
	}

	template<typename EPurchaseResult>
	FText GetUpgradeFeedback(EPurchaseResult Result)
	{
		switch (Result)
		{
		case EPurchaseResult::Purchased: return NSLOCTEXT("FlowUpgrades","Purchased","Upgrade purchased.");
		case EPurchaseResult::AlreadyPurchased: return NSLOCTEXT("FlowUpgrades","AlreadyPurchased","Upgrade already purchased.");
		case EPurchaseResult::InsufficientFunds: return NSLOCTEXT("FlowUpgrades","InsufficientFunds","The shop bank has insufficient funds.");
		case EPurchaseResult::RankChanged: return NSLOCTEXT("FlowUpgrades","RankChanged","The rank changed. Check the next upgrade and price.");
		case EPurchaseResult::MaxRank: return NSLOCTEXT("FlowUpgrades","MaxRank","This upgrade path is at maximum rank.");
		case EPurchaseResult::Unavailable: return NSLOCTEXT("FlowUpgrades","Unavailable","Upgrade unavailable.");
		}
		return FText::GetEmpty();
	}
}

void UShopTerminalWidget::BuildUpgradeMenu(UCanvasPanel* Canvas)
{
	auto* Scrim=WidgetTree->ConstructWidget<UBorder>();
	Scrim->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f,0.f,0.f,.46f),0.f));
	Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* ScrimSlot=Canvas->AddChildToCanvas(Scrim);
	ScrimSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));
	ScrimSlot->SetOffsets(FMargin(0.f));

	auto* Card=WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrush(bFlowUpgradeMenu
		? FSlateRoundedBoxBrush(FlowControllerUI::Panel,12.f,FlowControllerUI::Edge,1.f)
		: FSlateRoundedBoxBrush(FLinearColor(.025f,.065f,.08f,.99f),14.f));
	Card->SetPadding(bFlowUpgradeMenu ? FMargin(20.f,18.f) : FMargin(24.f,20.f));
	Card->SetRenderTransformPivot(FVector2D(.5f,.5f));
	CornerPanels.Add(Card);
	auto* CardSlot=Canvas->AddChildToCanvas(Card);
	CardSlot->SetAnchors(FAnchors(.5f,.5f));
	CardSlot->SetAlignment(FVector2D(.5f,.5f));
	CardSlot->SetAutoSize(true);
	CardSlot->SetPosition(FVector2D::ZeroVector);

	auto* Size=WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(bFlowUpgradeMenu ? 500.f : 492.f);
	Card->SetContent(Size);
	auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
	Size->SetContent(Column);
	if (bFlowUpgradeMenu)
	{
		BuildFlowUpgradePanel(*Column);
		return;
	}

	const auto AddLabel=[&](const TCHAR* Value,int32 PointSize,const FLinearColor& Color,bool bBold,const FMargin& SlotPadding)
	{
		auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Value));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular",PointSize));
		Label->SetColorAndOpacity(Color);
		Label->SetAutoWrapText(true);
		Column->AddChildToVerticalBox(Label)->SetPadding(SlotPadding);
		return Label;
	};
	const TCHAR* Title=bPlayerUpgradeMenu ? TEXT("Player upgrades") : TEXT("Filling table upgrades");
	AddLabel(Title,22,UpgradeCream,true,FMargin(0.f));
	Summary=AddLabel(TEXT(""),13,UpgradeMint,true,FMargin(0.f,4.f,0.f,14.f));

	if (bPlayerUpgradeMenu)
		AddLabel(TEXT("FULL: 1  ·  Includes partially filled bottles"),13,UpgradeMuted,false,FMargin(0.f,0.f,0.f,10.f));

	auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.f,0.f,0.f,15.f));
	FlowUpgradeTabs.Reset();
	const auto AddTab=[&](const TCHAR* Label,const TCHAR* Name)
	{
		auto* Tab=WidgetTree->ConstructWidget<UShopTerminalButton>();
		Tab->Configure(this,Label);
		Tab->SetToolTipText(FText::FromString(Name));
		if (auto* ContentSlot=Cast<UButtonSlot>(Tab->GetContentSlot())) ContentSlot->SetHorizontalAlignment(HAlign_Center);
		auto* TabSlot=Tabs->AddChildToHorizontalBox(Tab);
		TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TabSlot->SetPadding(FMargin(0.f,0.f,4.f,0.f));
		FlowUpgradeTabs.Add(Tab);
		return Tab;
	};
	if (bPlayerUpgradeMenu)
	{
		for (const auto& Branch : PlayerUpgradeBranches)
			AddTab(Branch.Label, Branch.Name)->PlayerUpgradeSelection = Branch.Type;
	}
	else
	{
		for (const auto& Branch:FillingUpgradeBranches) AddTab(Branch.Label,Branch.Name)->FillingUpgradeSelection=Branch.Type;
	}

	FlowUpgradeName=AddLabel(TEXT(""),15,UpgradeGold,true,FMargin(0.f));
	FlowUpgradeValue=AddLabel(TEXT(""),26,UpgradeCream,true,FMargin(0.f,3.f,0.f,6.f));
	FlowUpgradeDescription=AddLabel(TEXT(""),13,UpgradeMuted,false,FMargin(0.f,0.f,0.f,12.f));
	ResultText=AddLabel(TEXT(""),12,UpgradeMint,false,FMargin(0.f,0.f,0.f,5.f));

	auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Footer);
	auto* CloseButton=WidgetTree->ConstructWidget<UShopTerminalButton>();
	CloseButton->Configure(this,TEXT("Close  ·  Esc"));
	CloseButton->bClose=true;
	auto* CloseSlot=Footer->AddChildToHorizontalBox(CloseButton);
	CloseSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CloseSlot->SetHorizontalAlignment(HAlign_Left);

	BuyFlowUpgradeButton=WidgetTree->ConstructWidget<UShopTerminalButton>();
	BuyFlowUpgradeButton->Configure(this,TEXT("Buy upgrade"));
	BuyFlowUpgradeButton->bBuyFlowUpgrade=true;
	Footer->AddChildToHorizontalBox(BuyFlowUpgradeButton);
}

void UShopTerminalWidget::BuildFlowUpgradePanel(UVerticalBox& Column)
{
	const auto MakeLabel=[this](FName Name,int32 PointSize,const FLinearColor& Tint,bool bBold=false)
	{
		auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular",PointSize));
		Label->SetColorAndOpacity(Tint);
		return Label;
	};
	auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column.AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f,0.f,0.f,13.f));
	Header->AddChildToHorizontalBox(MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Controller,FlowControllerUI::Brass,28.f))->SetVerticalAlignment(VAlign_Center);
	auto* Title=MakeLabel(TEXT("FlowUpgradeHeading"),20,FlowControllerUI::Text,true);
	Title->SetText(NSLOCTEXT("FlowUpgrades","Heading","Upgrades"));
	auto* TitleSlot=Header->AddChildToHorizontalBox(Title);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	TitleSlot->SetPadding(FMargin(8.f,0.f));

	auto* BalancePanel=WidgetTree->ConstructWidget<UBorder>();
	BalancePanel->SetBrush(FSlateRoundedBoxBrush(FlowControllerUI::Inset,7.f,FlowControllerUI::Edge,1.f));
	BalancePanel->SetPadding(FMargin(9.f,6.f));
	BalancePanel->SetToolTipText(NSLOCTEXT("FlowUpgrades","Bank","Shared shop bank"));
	auto* BalanceSlot=Header->AddChildToHorizontalBox(BalancePanel);
	BalanceSlot->SetVerticalAlignment(VAlign_Center);
	BalanceSlot->SetPadding(FMargin(0.f,0.f,8.f,0.f));
	auto* BalanceRow=WidgetTree->ConstructWidget<UHorizontalBox>();
	BalancePanel->SetContent(BalanceRow);
	BalanceRow->AddChildToHorizontalBox(MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Coin,FlowControllerUI::Brass,16.f))->SetVerticalAlignment(VAlign_Center);
	Summary=MakeLabel(TEXT("FlowUpgradeBalance"),15,FlowControllerUI::Text,true);
	auto* BalanceTextSlot=BalanceRow->AddChildToHorizontalBox(Summary);
	BalanceTextSlot->SetVerticalAlignment(VAlign_Center);
	BalanceTextSlot->SetPadding(FMargin(6.f,0.f,0.f,0.f));

	auto* Close=WidgetTree->ConstructWidget<UShopTerminalButton>(UShopTerminalButton::StaticClass(),TEXT("FlowUpgradeClose"));
	Close->Configure(this,FString());
	Close->bClose=true;
	Close->SetStyle(MakeFlowUpgradeButtonStyle(FlowControllerUI::Surface,FlowControllerUI::Edge,FMargin(8.f)));
	Close->SetContent(MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Close,FlowControllerUI::Text,16.f));
	Close->SetToolTipText(NSLOCTEXT("FlowUpgrades","Close","Close · Esc"));
	Header->AddChildToHorizontalBox(Close)->SetVerticalAlignment(VAlign_Center);

	auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column.AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0.f,0.f,0.f,10.f));
	FlowUpgradeTabs.Reset();
	FlowUpgradeTabIcons.Reset();
	FlowUpgradeRankPips.Reset();
	RenderedFlowRank=INDEX_NONE;
	for (int32 Index=0; Index<UE_ARRAY_COUNT(FlowUpgradeBranches); ++Index)
	{
		const auto& Branch=FlowUpgradeBranches[Index];
		auto* Tab=WidgetTree->ConstructWidget<UShopTerminalButton>(UShopTerminalButton::StaticClass(),FName(Branch.WidgetName));
		Tab->Configure(this,FString());
		Tab->FlowUpgradeSelection=Branch.Type;
		Tab->SetToolTipText(FText::FromString(Branch.Name));
		auto* TabSlot=Tabs->AddChildToHorizontalBox(Tab);
		TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (Index+1<UE_ARRAY_COUNT(FlowUpgradeBranches)) TabSlot->SetPadding(FMargin(0.f,0.f,6.f,0.f));
		auto* TabContent=WidgetTree->ConstructWidget<UVerticalBox>();
		Tab->SetContent(TabContent);
		if (auto* ContentSlot=Cast<UButtonSlot>(Tab->GetContentSlot())) ContentSlot->SetHorizontalAlignment(HAlign_Center);
		auto* Icon=MakeFlowUpgradeIcon(*WidgetTree,Branch.Icon,FlowControllerUI::Muted,26.f);
		auto* IconSlot=TabContent->AddChildToVerticalBox(Icon);
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetPadding(FMargin(0.f,0.f,0.f,6.f));
		auto* RankRow=WidgetTree->ConstructWidget<UHorizontalBox>();
		TabContent->AddChildToVerticalBox(RankRow)->SetHorizontalAlignment(HAlign_Center);
		const int32 MaxRank=AColaFlowController::GetMaxUpgradeRank(Branch.Type);
		for (int32 Rank=0; Rank<MaxRank; ++Rank)
		{
			auto* PipSize=WidgetTree->ConstructWidget<USizeBox>();
			PipSize->SetWidthOverride(MaxRank>7 ? 4.f : 6.f);
			PipSize->SetHeightOverride(3.f);
			RankRow->AddChildToHorizontalBox(PipSize)->SetPadding(FMargin(1.f,0.f));
			auto* Pip=WidgetTree->ConstructWidget<UBorder>();
			Pip->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,1.5f));
			PipSize->SetContent(Pip);
			FlowUpgradeRankPips.Add(Pip);
		}
		FlowUpgradeTabs.Add(Tab);
		FlowUpgradeTabIcons.Add(Icon);
	}

	FlowUpgradeName=MakeLabel(TEXT("FlowUpgradeTitle"),14,FlowControllerUI::Brass,true);
	Column.AddChildToVerticalBox(FlowUpgradeName)->SetPadding(FMargin(0.f,0.f,0.f,6.f));
	auto* Comparison=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column.AddChildToVerticalBox(Comparison);
	const auto AddComparisonCard=[&](bool bNext,TObjectPtr<UTextBlock>& Value,TObjectPtr<UVerticalBox>& Graphic)
	{
		auto* Card=WidgetTree->ConstructWidget<UBorder>();
		Card->SetPadding(FMargin(10.f,7.f));
		Card->SetBrush(FSlateRoundedBoxBrush(bNext ? FlowControllerUI::Surface : FlowControllerUI::Inset,8.f,bNext ? FlowControllerUI::Mint*.36f : FlowControllerUI::Edge,1.f));
		Comparison->AddChildToHorizontalBox(Card)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Content=WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Content);
		Value=MakeLabel(bNext ? TEXT("FlowUpgradeNextValue") : TEXT("FlowUpgradeCurrentValue"),27,bNext ? FlowControllerUI::Mint : FlowControllerUI::Text,true);
		Value->SetJustification(ETextJustify::Center);
		Content->AddChildToVerticalBox(Value);
		auto* GraphicSize=WidgetTree->ConstructWidget<USizeBox>();
		GraphicSize->SetHeightOverride(34.f);
		Content->AddChildToVerticalBox(GraphicSize);
		Graphic=WidgetTree->ConstructWidget<UVerticalBox>();
		GraphicSize->SetContent(Graphic);
		if (auto* GraphicSlot=Cast<USizeBoxSlot>(Graphic->Slot)) GraphicSlot->SetVerticalAlignment(VAlign_Center);
		return Card;
	};
	AddComparisonCard(false,FlowUpgradeCurrentValue,FlowUpgradeCurrentGraphic);
	FlowUpgradeCompareArrow=MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::ArrowRight,FlowControllerUI::Brass,19.f);
	auto* ArrowSlot=Comparison->AddChildToHorizontalBox(FlowUpgradeCompareArrow);
	ArrowSlot->SetVerticalAlignment(VAlign_Center);
	ArrowSlot->SetPadding(FMargin(9.f,0.f));
	FlowUpgradeNextCard=AddComparisonCard(true,FlowUpgradeNextValue,FlowUpgradeNextGraphic);

	auto* EffectRow=WidgetTree->ConstructWidget<UHorizontalBox>();
	auto* EffectSlot=Column.AddChildToVerticalBox(EffectRow);
	EffectSlot->SetHorizontalAlignment(HAlign_Center);
	EffectSlot->SetPadding(FMargin(0.f,7.f,0.f,0.f));
	FlowUpgradeEffectIcon=MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Pressure,FlowControllerUI::Muted,15.f);
	EffectRow->AddChildToHorizontalBox(FlowUpgradeEffectIcon)->SetVerticalAlignment(VAlign_Center);
	FlowUpgradeEffectValue=MakeLabel(TEXT("FlowUpgradeEffectValue"),12,FlowControllerUI::Muted,true);
	EffectRow->AddChildToHorizontalBox(FlowUpgradeEffectValue)->SetPadding(FMargin(5.f,0.f,0.f,0.f));

	auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();
	Column.AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.f,8.f,0.f,0.f));
	FlowUpgradeStatusIcon=MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Lock,FlowControllerUI::Muted,18.f);
	Footer->AddChildToHorizontalBox(FlowUpgradeStatusIcon)->SetVerticalAlignment(VAlign_Center);
	ResultText=MakeLabel(TEXT("FlowUpgradeResult"),13,FlowControllerUI::Muted);
	auto* StatusSlot=Footer->AddChildToHorizontalBox(ResultText);
	StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	StatusSlot->SetVerticalAlignment(VAlign_Center);
	StatusSlot->SetPadding(FMargin(6.f,0.f,0.f,0.f));

	BuyFlowUpgradeButton=WidgetTree->ConstructWidget<UShopTerminalButton>(UShopTerminalButton::StaticClass(),TEXT("FlowUpgradeBuy"));
	BuyFlowUpgradeButton->Configure(this,FString());
	BuyFlowUpgradeButton->bBuyFlowUpgrade=true;
	BuyFlowUpgradeButton->SetStyle(MakeFlowUpgradeButtonStyle(FLinearColor(.04f,.16f,.13f,1.f),FlowControllerUI::Mint*.62f,FMargin(16.f,8.f)));
	auto* BuyContent=WidgetTree->ConstructWidget<UHorizontalBox>();
	BuyFlowUpgradeButton->SetContent(BuyContent);
	if (auto* ContentSlot=Cast<UButtonSlot>(BuyFlowUpgradeButton->GetContentSlot())) ContentSlot->SetHorizontalAlignment(HAlign_Center);
	FlowUpgradeBuyIcon=MakeFlowUpgradeIcon(*WidgetTree,EFlowControllerIcon::Coin,FlowControllerUI::Mint,18.f);
	BuyContent->AddChildToHorizontalBox(FlowUpgradeBuyIcon)->SetVerticalAlignment(VAlign_Center);
	FlowUpgradeBuyPrice=MakeLabel(TEXT("FlowUpgradeBuyPrice"),15,FlowControllerUI::Mint,true);
	auto* PriceSlot=BuyContent->AddChildToHorizontalBox(FlowUpgradeBuyPrice);
	PriceSlot->SetVerticalAlignment(VAlign_Center);
	PriceSlot->SetPadding(FMargin(7.f,0.f,0.f,0.f));
	Footer->AddChildToHorizontalBox(BuyFlowUpgradeButton);
}

void UShopTerminalWidget::RefreshPlayerUpgradeMenu()
{
	if (!BuyFlowUpgradeButton) return;
	auto* Upgrades = UPlayerUpgradesComponent::Find(Actions.IsValid() ? Actions->GetOwner() : nullptr);
	if (PlayerUpgrades.Get() != Upgrades)
	{
		if (auto* Previous = PlayerUpgrades.Get())
			Previous->OnPurchaseResult.RemoveDynamic(this, &UShopTerminalWidget::PlayerUpgradePurchaseResult);
		PlayerUpgrades = Upgrades;
		if (Upgrades)
			Upgrades->OnPurchaseResult.AddUniqueDynamic(this, &UShopTerminalWidget::PlayerUpgradePurchaseResult);
		PendingUpgradeOperation.Invalidate();
		bWaitingForPurchasedRank = false;
	}

	const auto& Branch = PlayerUpgradeBranches[static_cast<uint8>(SelectedPlayerUpgrade)];
	if (!Upgrades)
	{
		DisplayedUpgradeRank = INDEX_NONE;
		FlowUpgradeName->SetText(FText::FromString(Branch.Name));
		FlowUpgradeValue->SetText(FText::GetEmpty());
		UpgradeFeedback = NSLOCTEXT("PlayerUpgrades", "Loading", "Loading player upgrades…");
		RefreshUpgradePurchaseButton(0, false, false);
		return;
	}
	if (DisplayedUpgradeRank == INDEX_NONE)
		UpgradeFeedback = FText::GetEmpty();

	if (bWaitingForPurchasedRank && Upgrades->GetUpgradeRank(PendingPlayerUpgrade) > PendingUpgradeRank)
	{
		PendingUpgradeOperation.Invalidate();
		bWaitingForPurchasedRank = false;
	}

	DisplayedUpgradeRank = Upgrades->GetUpgradeRank(Branch.Type);
	const int32 MaxRank = Upgrades->GetMaxUpgradeRank(Branch.Type);
	const bool bMaxed = DisplayedUpgradeRank >= MaxRank;
	FlowUpgradeName->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  Rank %d/%d"),
		Branch.Name, DisplayedUpgradeRank, MaxRank)));
	FString Value = GetPlayerUpgradeValueText(Branch.Type, Upgrades->GetUpgradeValue(Branch.Type, DisplayedUpgradeRank));
	if (!bMaxed)
		Value += TEXT(" → ") + GetPlayerUpgradeValueText(Branch.Type, Upgrades->GetUpgradeValue(Branch.Type, DisplayedUpgradeRank + 1));
	FlowUpgradeValue->SetText(FText::FromString(Value + Branch.Unit));

	for (int32 Index = 0; Index < FlowUpgradeTabs.Num(); ++Index)
	{
		auto* Tab = FlowUpgradeTabs[Index].Get();
		const bool bSelected = Tab->PlayerUpgradeSelection.GetValue() == Branch.Type;
		SetFlowUpgradeButtonText(*Tab, PlayerUpgradeBranches[Index].Label, bSelected ? UpgradeGold : UpgradeCream);
		Tab->SetBackgroundColor(bSelected ? FLinearColor(1.6f, 1.25f, .9f) : FLinearColor::White);
	}

	RefreshUpgradePurchaseButton(Upgrades->GetNextUpgradeCostCents(Branch.Type), bMaxed, false);
}

void UShopTerminalWidget::HandlePlayerUpgradeButton(const UShopTerminalButton& Button)
{
	if (Button.PlayerUpgradeSelection.IsSet())
	{
		SelectedPlayerUpgrade = Button.PlayerUpgradeSelection.GetValue();
		UpgradeFeedback = FText::GetEmpty();
		RefreshPlayerUpgradeMenu();
		return;
	}

	auto* Upgrades = PlayerUpgrades.Get();
	if (!Upgrades || &Button != BuyFlowUpgradeButton.Get() || !Button.GetIsEnabled() ||
		PendingUpgradeOperation.IsValid() || DisplayedUpgradeRank == INDEX_NONE) return;
	PendingUpgradeOperation = FGuid::NewGuid();
	PendingPlayerUpgrade = SelectedPlayerUpgrade;
	PendingUpgradeRank = DisplayedUpgradeRank;
	bWaitingForPurchasedRank = false;
	UpgradeFeedback = FText::GetEmpty();
	RefreshPlayerUpgradeMenu();
	if (!Upgrades->RequestPurchase(PendingPlayerUpgrade, PendingUpgradeRank, PendingUpgradeOperation))
		PlayerUpgradePurchaseResult(PendingUpgradeOperation, EPlayerUpgradePurchaseResult::Unavailable);
}

void UShopTerminalWidget::PlayerUpgradePurchaseResult(FGuid OperationID, EPlayerUpgradePurchaseResult Result)
{
	if (!PendingUpgradeOperation.IsValid() || PendingUpgradeOperation != OperationID) return;
	bWaitingForPurchasedRank = Result == EPlayerUpgradePurchaseResult::Purchased ||
		Result == EPlayerUpgradePurchaseResult::AlreadyPurchased;
	if (!bWaitingForPurchasedRank) PendingUpgradeOperation.Invalidate();
	UpgradeFeedback = GetUpgradeFeedback(Result);
	RefreshPlayerUpgradeMenu();
}

void UShopTerminalWidget::RefreshFlowUpgradeMenu(const AColaFlowController& Flow)
{
	if (!BuyFlowUpgradeButton || !FlowUpgradeCurrentValue) return;
	if (bWaitingForPurchasedRank && Flow.GetUpgradeRank(PendingFlowUpgrade)>PendingUpgradeRank)
	{
		PendingUpgradeOperation.Invalidate();
		bWaitingForPurchasedRank=false;
	}

	const auto& Branch=GetFlowUpgradeBranch(SelectedFlowUpgrade);
	const int32 Rank=Flow.GetUpgradeRank(Branch.Type);
	const int32 MaxRank=Flow.GetMaxUpgradeRank(Branch.Type);
	const bool bMaxed=Rank>=MaxRank;
	const int32 NextRank=bMaxed ? Rank : Rank+1;
	const bool bSelectionChanged=RenderedFlowRank==INDEX_NONE || RenderedFlowUpgrade!=Branch.Type;
	DisplayedUpgradeRank=Rank;
	if (bSelectionChanged || RenderedFlowRank!=Rank)
	{
		RenderedFlowUpgrade=Branch.Type;
		RenderedFlowRank=Rank;
		FlowUpgradeName->SetText(FText::FromString(Branch.Label));
		const FText Description=FText::FromString(GetFlowUpgradeDescription(Flow,Branch.Type,Rank,NextRank,GetOwningLocalPlayer()));
		FlowUpgradeName->SetToolTipText(Description);
		FlowUpgradeCurrentValue->SetText(FText::FromString(GetFlowUpgradeValueText(Flow,Branch.Type,Rank)));
		FlowUpgradeCurrentValue->SetToolTipText(FText::FromString(FString::Printf(TEXT("Current rank %d of %d"),Rank,MaxRank)));
		BuildFlowUpgradeGraphic(*WidgetTree,*FlowUpgradeCurrentGraphic,Flow,Branch.Type,Rank,MaxRank,FlowControllerUI::Brass);
		FlowUpgradeNextCard->SetVisibility(bMaxed ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		FlowUpgradeCompareArrow->SetVisibility(bMaxed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (!bMaxed)
		{
			FlowUpgradeNextValue->SetText(FText::FromString(GetFlowUpgradeValueText(Flow,Branch.Type,NextRank)));
			FlowUpgradeNextCard->SetToolTipText(Description);
			BuildFlowUpgradeGraphic(*WidgetTree,*FlowUpgradeNextGraphic,Flow,Branch.Type,NextRank,MaxRank,FlowControllerUI::Mint);
		}
		FString Effect;
		if (Branch.Type==EFlowControllerUpgradeType::Flow)
		{
			Effect=FString::Printf(TEXT("%.0f%%"),100.f*Flow.GetInletToleranceMultiplier(Rank));
			if (!bMaxed) Effect+=FString::Printf(TEXT(" → %.0f%%"),100.f*Flow.GetInletToleranceMultiplier(NextRank));
		}
		else if (Branch.Type==EFlowControllerUpgradeType::Suction)
		{
			const auto ReductionText=[](int32 Reduction)
			{
				return Reduction>0 ? FString::Printf(TEXT("−%d%%"),Reduction) : FString(TEXT("0%"));
			};
			Effect=ReductionText(GetSuctionReduction(Flow,Rank));
			if (!bMaxed) Effect+=TEXT(" → ")+ReductionText(GetSuctionReduction(Flow,NextRank));
		}
		FlowUpgradeEffectValue->SetText(FText::FromString(Effect));
		FlowUpgradeEffectValue->SetToolTipText(Description);
		FlowUpgradeEffectValue->SetVisibility(Effect.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
		FlowUpgradeEffectIcon->SetVisibility(Effect.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
	int32 Offset=0;
	for (int32 Index=0; Index<FlowUpgradeTabs.Num(); ++Index)
	{
		const auto& TabBranch=FlowUpgradeBranches[Index];
		const int32 Purchased=Flow.GetUpgradeRank(TabBranch.Type);
		const int32 TabMaxRank=Flow.GetMaxUpgradeRank(TabBranch.Type);
		const bool bSelected=TabBranch.Type==Branch.Type;
		auto* Tab=FlowUpgradeTabs[Index].Get();
		if (bSelectionChanged)
		{
			Tab->SetStyle(MakeFlowUpgradeButtonStyle(bSelected ? FlowControllerUI::Surface : FlowControllerUI::Inset,bSelected ? FlowControllerUI::Brass : FlowControllerUI::Edge,FMargin(7.f,8.f)));
			FlowUpgradeTabIcons[Index]->SetTint(bSelected ? FlowControllerUI::Brass : FlowControllerUI::Muted);
		}
		Tab->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s · %d/%d"),TabBranch.Name,Purchased,TabMaxRank)));
		for (int32 SlotIndex=0; SlotIndex<TabMaxRank; ++SlotIndex)
			FlowUpgradeRankPips[Offset++]->SetBrushColor(SlotIndex<Purchased ? FlowControllerUI::Mint : bSelected && SlotIndex==Purchased ? FlowControllerUI::Brass : FlowControllerUI::Edge);
	}

	RefreshUpgradePurchaseButton(Flow.GetNextUpgradeCost(Branch.Type),bMaxed,Flow.bLeverHeld);
}

void UShopTerminalWidget::RefreshUpgradePurchaseButton(int64 Cost, bool bMaxed, bool bBusy)
{
	TActorIterator<APropManager> Manager(GetWorld());
	const auto* Wallet=Manager ? Manager->SharedWallet.Get() : nullptr;
	const int64 Balance=Wallet ? Wallet->GetWalletState().BalanceCents : 0;
	const bool bPending=PendingUpgradeOperation.IsValid();
	const bool bCanBuy=Wallet && !bMaxed && Cost>0 && Balance>=Cost && !bPending && !bBusy;
	BuyFlowUpgradeButton->SetIsEnabled(bCanBuy);
	if (bFlowUpgradeMenu)
	{
		RefreshFlowUpgradePurchase(Cost,Balance,Wallet!=nullptr,bMaxed,bBusy);
		return;
	}
	Summary->SetText(FText::FromString(Wallet ? TEXT("Shop bank  ")+ShopTerminal::Money(Balance) : TEXT("Shop bank unavailable")));
	const FString BuyLabel=bMaxed ? TEXT("Max rank") : Cost>0 ? TEXT("Buy for ")+ShopTerminal::Money(Cost) : TEXT("Unavailable");
	SetFlowUpgradeButtonText(*BuyFlowUpgradeButton,BuyLabel,bCanBuy ? UpgradeMint : UpgradeMuted);

	FText Feedback=UpgradeFeedback;
	if (bPending && !bWaitingForPurchasedRank) Feedback=NSLOCTEXT("FlowUpgrades","Pending","Purchasing…");
	if (Feedback.IsEmpty() && !bMaxed)
	{
		if (bBusy) Feedback=NSLOCTEXT("FlowUpgrades","Busy","Release the lever to buy upgrades.");
		else if (Wallet && Cost>Balance)
			Feedback=FText::FromString(TEXT("Shop bank needs ")+ShopTerminal::Money(Cost-Balance)+TEXT(" more."));
	}
	ResultText->SetText(Feedback);
}

void UShopTerminalWidget::RefreshFlowUpgradePurchase(int64 Cost,int64 Balance,bool bHasWallet,bool bMaxed,bool bBusy)
{
	Summary->SetText(FText::FromString(bHasWallet ? ShopTerminal::Money(Balance) : FString(TEXT("—"))));
	const bool bPending=PendingUpgradeOperation.IsValid() && !bWaitingForPurchasedRank;
	const FLinearColor BuyTint=BuyFlowUpgradeButton->GetIsEnabled() ? FlowControllerUI::Mint : FlowControllerUI::Muted;
	FlowUpgradeBuyIcon->SetIcon(bMaxed ? EFlowControllerIcon::Check : bPending ? EFlowControllerIcon::Clock : EFlowControllerIcon::Coin);
	FlowUpgradeBuyIcon->SetTint(BuyTint);
	FlowUpgradeBuyPrice->SetColorAndOpacity(BuyTint);
	FlowUpgradeBuyPrice->SetText(bMaxed ? NSLOCTEXT("FlowUpgrades","Max","Max")
		: FText::FromString(Cost>0 ? ShopTerminal::Money(Cost) : FString(TEXT("—"))));
	BuyFlowUpgradeButton->SetToolTipText(bMaxed ? NSLOCTEXT("FlowUpgrades","MaxRank","This upgrade path is at maximum rank.")
		: Cost>0 ? FText::Format(NSLOCTEXT("FlowUpgrades","BuyCost","Buy next rank for {0}"),FText::FromString(ShopTerminal::Money(Cost)))
		: NSLOCTEXT("FlowUpgrades","Unavailable","Upgrade unavailable."));

	FText Status;
	FText Explanation=UpgradeFeedback;
	EFlowControllerIcon StatusIcon=EFlowControllerIcon::Lock;
	FLinearColor StatusTint=FlowControllerUI::Brass;
	if (bPending)
	{
		Status=NSLOCTEXT("FlowUpgrades","PendingShort","Buying…");
		StatusIcon=EFlowControllerIcon::Clock;
	}
	else if (LastFlowUpgradeResult.IsSet())
	{
		switch (LastFlowUpgradeResult.GetValue())
		{
		case EFlowControllerPurchaseResult::Purchased:
		case EFlowControllerPurchaseResult::AlreadyPurchased:
			Status=NSLOCTEXT("FlowUpgrades","BoughtShort","Purchased");
			StatusIcon=EFlowControllerIcon::Check;
			StatusTint=FlowControllerUI::Mint;
			break;
		case EFlowControllerPurchaseResult::InsufficientFunds:
			Status=Cost>Balance ? FText::Format(NSLOCTEXT("FlowUpgrades","NeedCost","Need {0}"),FText::FromString(ShopTerminal::Money(Cost-Balance)))
				: NSLOCTEXT("FlowUpgrades","FundsShort","Insufficient funds");
			break;
		case EFlowControllerPurchaseResult::RankChanged:
			Status=NSLOCTEXT("FlowUpgrades","RankChangedShort","Rank changed");
			StatusIcon=EFlowControllerIcon::Warning;
			break;
		case EFlowControllerPurchaseResult::MaxRank:
			Status=NSLOCTEXT("FlowUpgrades","Max","Max");
			StatusIcon=EFlowControllerIcon::Check;
			break;
		case EFlowControllerPurchaseResult::Unavailable:
			Status=NSLOCTEXT("FlowUpgrades","UnavailableShort","Unavailable");
			StatusIcon=EFlowControllerIcon::Warning;
			StatusTint=FlowControllerUI::Red;
			break;
		}
	}
	else if (!bMaxed)
	{
		if (bBusy)
		{
			Status=NSLOCTEXT("FlowUpgrades","BusyShort","Lever active");
			Explanation=NSLOCTEXT("FlowUpgrades","Busy","Release the lever to buy upgrades.");
		}
		else if (!bHasWallet || Cost<=0)
		{
			Status=NSLOCTEXT("FlowUpgrades","UnavailableShort","Unavailable");
			StatusIcon=EFlowControllerIcon::Warning;
		}
		else if (Cost>Balance)
			Status=FText::Format(NSLOCTEXT("FlowUpgrades","NeedCost","Need {0}"),FText::FromString(ShopTerminal::Money(Cost-Balance)));
	}
	FlowUpgradeStatusIcon->SetIcon(StatusIcon);
	FlowUpgradeStatusIcon->SetTint(StatusTint);
	FlowUpgradeStatusIcon->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	ResultText->SetText(Status);
	ResultText->SetColorAndOpacity(StatusTint);
	ResultText->SetToolTipText(Explanation);
}

void UShopTerminalWidget::HandleFlowUpgradeButton(const UShopTerminalButton& Button, AColaFlowController& Flow)
{
	if (Button.FlowUpgradeSelection.IsSet())
	{
		SelectedFlowUpgrade=Button.FlowUpgradeSelection.GetValue();
		LastFlowUpgradeResult.Reset();
		UpgradeFeedback=FText::GetEmpty();
		RefreshFlowUpgradeMenu(Flow);
		return;
	}
	if (!Button.bBuyFlowUpgrade || !Button.GetIsEnabled() || PendingUpgradeOperation.IsValid() || DisplayedUpgradeRank<0) return;
	PendingUpgradeOperation=FGuid::NewGuid();
	PendingFlowUpgrade=SelectedFlowUpgrade;
	PendingUpgradeRank=DisplayedUpgradeRank;
	bWaitingForPurchasedRank=false;
	LastFlowUpgradeResult.Reset();
	UpgradeFeedback=FText::GetEmpty();
	RefreshFlowUpgradeMenu(Flow);
	if (!Actions->RequestFlowControllerUpgrade(&Flow,PendingFlowUpgrade,PendingUpgradeRank,PendingUpgradeOperation))
		FlowUpgradePurchaseResult(PendingUpgradeOperation,EFlowControllerPurchaseResult::Unavailable);
}

void UShopTerminalWidget::FlowUpgradePurchaseResult(FGuid OperationID,EFlowControllerPurchaseResult Result)
{
	if (!PendingUpgradeOperation.IsValid() || PendingUpgradeOperation!=OperationID) return;
	bWaitingForPurchasedRank=Result==EFlowControllerPurchaseResult::Purchased || Result==EFlowControllerPurchaseResult::AlreadyPurchased;
	if (!bWaitingForPurchasedRank) PendingUpgradeOperation.Invalidate();

	LastFlowUpgradeResult=Result;
	UpgradeFeedback=GetUpgradeFeedback(Result);
	if (const auto* Flow=Cast<AColaFlowController>(Station.Get())) RefreshFlowUpgradeMenu(*Flow);
}

void UShopTerminalWidget::RefreshFillingUpgradeMenu(const ASocketColaFillingMachine& Table)
{
	if (!BuyFlowUpgradeButton) return;
	if (bWaitingForPurchasedRank && Table.GetUpgradeRank(PendingFillingUpgrade)>PendingUpgradeRank)
	{
		PendingUpgradeOperation.Invalidate();
		bWaitingForPurchasedRank=false;
	}

	const auto& Branch=GetFillingUpgradeBranch(SelectedFillingUpgrade);
	const int32 Rank=Table.GetUpgradeRank(Branch.Type);
	const int32 MaxRank=Table.GetMaxUpgradeRank(Branch.Type);
	const bool bMaxed=Rank>=MaxRank;
	const bool bFilling=Table.Production && (Table.Production->View.bLeverHeld || Table.Production->View.bFilling);
	DisplayedUpgradeRank=Rank;
	FlowUpgradeName->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  Rank %d/%d"),Branch.Name,Rank,MaxRank)));
	FString Value=GetFillingUpgradeValueText(Branch.Type,Table.GetUpgradeValue(Branch.Type,Rank));
	if (!bMaxed) Value+=TEXT(" → ")+GetFillingUpgradeValueText(Branch.Type,Table.GetUpgradeValue(Branch.Type,Rank+1));
	FlowUpgradeValue->SetText(FText::FromString(Value+Branch.Unit));
	FlowUpgradeDescription->SetText(FText::FromString(Branch.Description));
	for (UShopTerminalButton* Tab:FlowUpgradeTabs)
	{
		const auto& TabBranch=GetFillingUpgradeBranch(Tab->FillingUpgradeSelection.GetValue());
		const bool bSelected=TabBranch.Type==Branch.Type;
		SetFlowUpgradeButtonText(*Tab,TabBranch.Label,bSelected ? UpgradeGold : UpgradeCream);
		Tab->SetBackgroundColor(bSelected ? FLinearColor(1.6f,1.25f,.9f) : FLinearColor::White);
	}

	RefreshUpgradePurchaseButton(Table.GetNextUpgradeCostCents(Branch.Type),bMaxed,bFilling);
}

void UShopTerminalWidget::HandleFillingUpgradeButton(const UShopTerminalButton& Button, ASocketColaFillingMachine& Table)
{
	if (Button.FillingUpgradeSelection.IsSet())
	{
		SelectedFillingUpgrade=Button.FillingUpgradeSelection.GetValue();
		UpgradeFeedback=FText::GetEmpty();
		RefreshFillingUpgradeMenu(Table);
		return;
	}
	if (&Button!=BuyFlowUpgradeButton.Get() || !Button.GetIsEnabled() || PendingUpgradeOperation.IsValid() || DisplayedUpgradeRank<0) return;
	PendingUpgradeOperation=FGuid::NewGuid();
	PendingFillingUpgrade=SelectedFillingUpgrade;
	PendingUpgradeRank=DisplayedUpgradeRank;
	bWaitingForPurchasedRank=false;
	UpgradeFeedback=FText::GetEmpty();
	RefreshFillingUpgradeMenu(Table);
	if (!Actions->RequestFillingMachineUpgrade(&Table,PendingFillingUpgrade,PendingUpgradeRank,PendingUpgradeOperation))
		FillingUpgradePurchaseResult(PendingUpgradeOperation,EFillingMachinePurchaseResult::Unavailable);
}

void UShopTerminalWidget::FillingUpgradePurchaseResult(FGuid OperationID,EFillingMachinePurchaseResult Result)
{
	if (!PendingUpgradeOperation.IsValid() || PendingUpgradeOperation!=OperationID) return;
	bWaitingForPurchasedRank=Result==EFillingMachinePurchaseResult::Purchased || Result==EFillingMachinePurchaseResult::AlreadyPurchased;
	if (!bWaitingForPurchasedRank) PendingUpgradeOperation.Invalidate();
	UpgradeFeedback=GetUpgradeFeedback(Result);
	if (const auto* Table=Cast<ASocketColaFillingMachine>(Station.Get())) RefreshFillingUpgradeMenu(*Table);
}
