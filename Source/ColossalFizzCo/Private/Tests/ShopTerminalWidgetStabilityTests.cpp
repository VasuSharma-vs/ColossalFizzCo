#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Algo/AllOf.h"
#include "Algo/Count.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "UI/FlowControllerIcon.h"
#include "UI/ShopTerminalWidget.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ColaFlowController.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "ShopCashTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTerminalWidgetStabilityTest,"ColossalFizzCo.Shop.TerminalWidgetStability",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopTerminalWidgetStabilityTest::RunTest(const FString& Parameters)
{
	auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World=GI->GetWorld();
	if (!World) return false;
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false;
	FPropProductDefinition Product; Product.ProductID=21; Product.bEnableInstanceUniqueData=true;
	Product.ConsumableProfile.NumOfPortions=4; Product.DefaultUniqueData.EventData.ContentID=7; Manager->ProductDefinitions.Add(21,Product);
	FPropContentDefinition Content; Content.ContentID=7; Content.DisplayName=FText::FromString(TEXT("Test Cola"));
	Content.BasePortionPrice=4; Content.IdealServingTemperature=4; Content.ExpiryProfile.bHasExpiry=false;
	Manager->ContentDefinitions.Add(7,Content);
	FISIDRuntimeIndex Record; Record.ISID=10001; Record.ProductID=21; Record.PackedRuntimeData.RemainingPortions=4;
	Manager->RegisterISID(Record); Manager->ShopCheckout->RegisterShopStock(10001,0,FTransform::Identity);
	auto* Station=World->SpawnActor<AActor>();
	auto* Scanner=NewObject<UShopScannerComponent>(Station); Station->AddInstanceComponent(Scanner); Scanner->RegisterComponent();
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* PC=World->SpawnActor<APlayerController>(); PC->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	Scanner->AssignCustomer(FGuid::NewGuid());
	TestEqual(TEXT("Real bottle enters scanner"),Scanner->ScanProduct(10001),EShopCheckoutResult::Success);
	auto* Widget=CreateWidget<UShopTerminalWidget>(GI,UShopTerminalWidget::StaticClass());
	if (!TestNotNull(TEXT("Native terminal widget initializes"),Widget)) return false;
	Widget->Configure(Actions,Station); const auto Slate=Widget->TakeWidget(); Widget->Refresh();
	if (!TestNotNull(TEXT("Terminal body exists"),Widget->Body.Get())) return false;
	TestTrue(TEXT("First visible frame has checkout controls"),Widget->Body->GetChildrenCount()>3);
	const auto Scanning=Widget->Body->GetAllChildren(); Widget->Refresh();
	TestTrue(TEXT("Repeated scans retain existing controls"),Widget->Body->GetAllChildren()==Scanning);
	if (!TestEqual(TEXT("Real scanner locks bill"),Scanner->BeginCheckout(),EShopCheckoutResult::Success)) return false;
	Widget->Refresh(); const auto Payment=Widget->Body->GetAllChildren();
	TestTrue(TEXT("Phase change replaces controls within same widget"),Payment!=Scanning);
	const auto State=Scanner->GetScannerState();
	ShopCashTests::FPayment Cash(Manager,State.Checkout.CheckoutID,State.Checkout.TotalCents);
	TestEqual(TEXT("Physical tender updates the existing checkout"),Scanner->GetScannerState().Checkout.TenderCents,State.Checkout.TotalCents);
	Widget->Refresh();
	TestTrue(TEXT("Payment text update retains button identities and focus targets"),Widget->Body->GetAllChildren()==Payment);
	TestTrue(TEXT("Duplicate station identity is recognized"),Widget->IsShowingStation(Station));
	Widget->Close();

	auto* Flow=World->SpawnActor<AColaFlowController>();
	TestNull(TEXT("The controller has no floating status text"),Flow->GetDefaultSubobjectByName(TEXT("FlowDisplay")));
	FFlowControllerUpgradeState Ranks;
	Ranks.InputPortsRank=1;
	Ranks.OutputPortsRank=1;
	Ranks.FlowRank=3;
	Flow->RestoreUpgradeState(Ranks);
	Flow->bLeverHeld=true;
	Flow->SelectedInputIndex=1;
	Flow->SelectedContentID=Content.ContentID;
	Flow->InletPortionsPerSecond=2.5f;
	Flow->OutletPortionsPerSecond=1.25f;
	Flow->PortionsPerSecondPerFiller=.5f;
	Flow->ConnectedOutletCount=2;
	Flow->InletSupplyPressure=2.f;
	Flow->PressurePerOutlet=1.f;
	Flow->ActiveFillerCount=2;
	auto* FlowWidget=CreateWidget<UShopTerminalWidget>(GI,UShopTerminalWidget::StaticClass());
	if (!TestNotNull(TEXT("Flow HUD widget initializes"),FlowWidget)) return false;
	FlowWidget->Configure(Actions,Flow);
	const auto FlowSlate=FlowWidget->TakeWidget();
	FlowWidget->Refresh();
	TestEqual(TEXT("Flow HUD leaves pointer input in gameplay"),FlowWidget->GetVisibility(),ESlateVisibility::HitTestInvisible);
	if (!TestNotNull(TEXT("Flow HUD has a flavor label"),FlowWidget->FlowHudFlavor.Get())
		|| !TestNotNull(TEXT("Flow HUD has an inlet meter"),FlowWidget->FlowHudInletMeter.Get())
		|| !TestNotNull(TEXT("Flow HUD has an outlet meter"),FlowWidget->FlowHudOutletMeter.Get())) return false;
	TestEqual(TEXT("The readout identifies the selected cola"),FlowWidget->FlowHudFlavor->GetText().ToString(),FString(TEXT("Test Cola")));
	const auto Reading=[](const UTextBlock* Number) { return Number ? FCString::Atof(*Number->GetText().ToString()) : -1.f; };
	TestEqual(TEXT("The inlet pressure dial retains its numeric reading"),Reading(FlowWidget->FlowHudInPressure),2.f);
	TestEqual(TEXT("The outlet pressure dial retains its numeric reading"),Reading(FlowWidget->FlowHudOutPressure),1.f);
	TestEqual(TEXT("The inlet meter retains its numeric reading"),Reading(FlowWidget->FlowHudInFlow),2.5f);
	TestEqual(TEXT("The outlet meter retains its numeric reading"),Reading(FlowWidget->FlowHudOutFlow),1.25f);
	TestEqual(TEXT("Allocated flow per outlet remains visible"),Reading(FlowWidget->FlowHudPerOutlet),.5f);
	TestEqual(TEXT("The bottle icon displays the active count"),Reading(FlowWidget->FlowHudBottleCount),2.f);
	TestEqual(TEXT("The inlet meter fills against the upgraded rate"),FlowWidget->FlowHudInletMeter->GetPercent(),.625f);
	TestEqual(TEXT("The outlet meter fills against the upgraded rate"),FlowWidget->FlowHudOutletMeter->GetPercent(),.3125f);
	const auto IsShown=[](const auto& Port) { return Port && Port->IsVisible(); };
	TestEqual(TEXT("The inlet strip shows the unlocked sockets"),Algo::CountIf(FlowWidget->FlowHudInPorts,IsShown),SIZE_T(2));
	TestEqual(TEXT("The outlet strip shows the unlocked sockets"),Algo::CountIf(FlowWidget->FlowHudOutPorts,IsShown),SIZE_T(2));
	if (FlowWidget->FlowHudInPorts.Num()>=2)
		TestTrue(TEXT("The selected inlet marker identifies its state"),FlowWidget->FlowHudInPorts[1]->GetToolTipText().ToString().Contains(TEXT("selected")));
	if (!TestNotNull(TEXT("The readout has a lever mode icon"),FlowWidget->FlowHudLeverIcon.Get())
		|| !TestNotNull(TEXT("The readout has a lever status icon"),FlowWidget->FlowHudStatusIcon.Get())) return false;
	TestTrue(TEXT("The lever mode identifies hold-to-fill"),FlowWidget->FlowHudLeverIcon->GetIcon()==EFlowControllerIcon::Hold);
	TestTrue(TEXT("A lever operated by another player displays a lock"),FlowWidget->FlowHudStatusIcon->GetIcon()==EFlowControllerIcon::Lock);
	Flow->bLeverHeld=false;
	FlowWidget->Refresh();
	if (!TestNotNull(TEXT("The lever has a keycap"),FlowWidget->FlowHudLeverKey.Get())
		|| !TestNotNull(TEXT("Upgrades have a keycap"),FlowWidget->FlowHudUpgradeKey.Get())
		|| !TestNotNull(TEXT("Placement has a keycap"),FlowWidget->FlowHudMoveKey.Get())) return false;
	TestEqual(TEXT("The lever keycap shows the default interaction key"),FlowWidget->FlowHudLeverKey->GetText().ToString(),FString(TEXT("E")));
	TestEqual(TEXT("The upgrade keycap shows the default interaction key"),FlowWidget->FlowHudUpgradeKey->GetText().ToString(),FString(TEXT("E")));
	TestEqual(TEXT("The placement keycap shows the move key"),FlowWidget->FlowHudMoveKey->GetText().ToString(),FString(TEXT("M")));

	auto* UpgradeMenu=CreateWidget<UShopTerminalWidget>(GI,UShopTerminalWidget::StaticClass());
	if (!TestNotNull(TEXT("The Flow Controller upgrade menu initializes"),UpgradeMenu)) return false;
	UpgradeMenu->Configure(Actions,Flow,true);
	const auto UpgradeSlate=UpgradeMenu->TakeWidget();
	UpgradeMenu->Refresh();
	TestTrue(TEXT("The upgrade menu takes input separately from the passive readout"),UpgradeMenu->IsFlowUpgradeMenu() && !UpgradeMenu->IsWorldReadout());
	if (!TestEqual(TEXT("The menu shows all five upgrade branches"),UpgradeMenu->FlowUpgradeTabs.Num(),5)
		|| !TestNotNull(TEXT("The menu has a buy button"),UpgradeMenu->BuyFlowUpgradeButton.Get())
		|| !TestNotNull(TEXT("The current card has a value"),UpgradeMenu->FlowUpgradeCurrentValue.Get())
		|| !TestNotNull(TEXT("The next card has a value"),UpgradeMenu->FlowUpgradeNextValue.Get())) return false;
	TestTrue(TEXT("All five upgrade choices have graphical button contents"),Algo::AllOf(UpgradeMenu->FlowUpgradeTabs,[](const UShopTerminalButton* Tab)
		{ return Tab && Tab->GetContent() && !Tab->GetContent()->IsA<UTextBlock>(); }));
	TestEqual(TEXT("The current inlet card shows two available ports"),Reading(UpgradeMenu->FlowUpgradeCurrentValue),2.f);
	TestEqual(TEXT("The next inlet card shows three available ports"),Reading(UpgradeMenu->FlowUpgradeNextValue),3.f);
	TestTrue(TEXT("Both inlet cards present graphics"),UpgradeMenu->FlowUpgradeCurrentGraphic && UpgradeMenu->FlowUpgradeNextGraphic
		&& UpgradeMenu->FlowUpgradeCurrentGraphic->GetChildrenCount()>0 && UpgradeMenu->FlowUpgradeNextGraphic->GetChildrenCount()>0);
	TestFalse(TEXT("The zero-balance shop cannot buy an upgrade"),UpgradeMenu->BuyFlowUpgradeButton->GetIsEnabled());
	UpgradeMenu->HandleButton(UpgradeMenu->FlowUpgradeTabs[4]);
	TestEqual(TEXT("The current lever card shows hold mode"),UpgradeMenu->FlowUpgradeCurrentValue->GetText().ToString(),FString(TEXT("HOLD")));
	TestEqual(TEXT("The next lever card shows toggle mode"),UpgradeMenu->FlowUpgradeNextValue->GetText().ToString(),FString(TEXT("TOGGLE")));
	const int64 LeverPrice=Flow->GetNextUpgradeCost(EFlowControllerUpgradeType::Lever);
	TestTrue(TEXT("The first lever upgrade has a price"),LeverPrice>0);
	TestTrue(TEXT("The shop bank can fund the purchase"),Manager->SharedWallet->Credit(FGuid::NewGuid(),LeverPrice,TEXT("UpgradeWidgetTest"))==EWalletOperationResult::Applied);
	UpgradeMenu->Refresh();
	TestTrue(TEXT("The exact available balance enables purchase"),UpgradeMenu->BuyFlowUpgradeButton->GetIsEnabled());
	Flow->bLeverHeld=true;
	UpgradeMenu->Refresh();
	TestFalse(TEXT("The menu blocks purchases while the lever is active"),UpgradeMenu->BuyFlowUpgradeButton->GetIsEnabled());
	UpgradeMenu->Close();
	Flow->bLeverHeld=false;
	Ranks.LeverRank=1;
	Flow->RestoreUpgradeState(Ranks);
	FlowWidget->Refresh();
	TestEqual(TEXT("The toggle keycap retains the default interaction key"),FlowWidget->FlowHudLeverKey->GetText().ToString(),FString(TEXT("E")));
	TestTrue(TEXT("The lever mode reflects the toggle upgrade"),FlowWidget->FlowHudLeverIcon->GetIcon()==EFlowControllerIcon::Toggle);
	FlowWidget->Close();
	return true;
}
#endif
