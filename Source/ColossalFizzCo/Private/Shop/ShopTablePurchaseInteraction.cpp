#include "Shop/ShopPlayerInteractionComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Shop/ShopTableUpgradeTerminal.h"
#include "UI/SettingsControls.h"
#include "UI/ShopTablePurchaseWidget.h"
#include "UI/ShopTerminalWidget.h"

bool UShopPlayerInteractionComponent::IsTableUpgradePickerOpen() const
{
	return IsValid(TableUpgradePicker) && TableUpgradePicker->IsInViewport();
}

bool UShopPlayerInteractionComponent::OpenTableUpgradePicker(APlayerController& Controller, AShopTableUpgradeTerminal& Station)
{
	if (IsTableUpgradePickerOpen()) return true;
	const auto* LocalPlayer = Controller.GetLocalPlayer();
	if (!LocalPlayer || (IsTerminalOpen() && !TerminalWidget->IsWorldReadout()) || bMachinePlacementKeyDown
		|| GetLocalFlowLever() || Controller.IsInputKeyDown(SettingsControls::GrabKey(LocalPlayer))) return false;

	auto* Picker = CreateWidget<UShopTablePurchaseWidget>(&Controller, UShopTablePurchaseWidget::StaticClass());
	if (!Picker) return false;

	const TWeakObjectPtr<UShopPlayerInteractionComponent> WeakActions(this);
	Picker->InitializePicker(
		[WeakActions](FName OfferID, int64 PriceCents, FGuid OperationID)
		{
			auto* Actions = WeakActions.Get();
			return Actions && Actions->RequestTablePurchase(OfferID, PriceCents, OperationID);
		},
		[WeakActions]()
		{
			if (auto* Actions = WeakActions.Get()) Actions->CloseTableUpgradePicker();
		});
	if (!Picker->AddToPlayerScreen(60)) return false;

	TableUpgradePicker = Picker;
	TableUpgradeTerminal = &Station;
	CloseFlowReadout();
	CloseFillingTableReadout();
	CloseBottleDispenserReadout();
	Picker->SetAnchorsInViewport(FAnchors(0, 0, 1, 1));
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Picker->TakeWidget());
	Controller.SetInputMode(Mode);
	Controller.SetShowMouseCursor(true);
	return true;
}

void UShopPlayerInteractionComponent::CloseTableUpgradePicker()
{
	TableUpgradeTerminal.Reset();
	if (!IsValid(TableUpgradePicker)) return;

	auto* Picker = TableUpgradePicker.Get();
	TableUpgradePicker = nullptr;
	Picker->RemoveFromParent();
	if (auto* Controller = Picker->GetOwningPlayer(); Controller && Controller->IsLocalController())
	{
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->SetShowMouseCursor(false);
	}
}

bool UShopPlayerInteractionComponent::RequestTablePurchase(FName OfferID, int64 ExpectedPriceCents, FGuid OperationID)
{
	auto* Station = TableUpgradeTerminal.Get();
	if (!IsTableUpgradePickerOpen() || !Station || !GetOwner() || OfferID.IsNone() || ExpectedPriceCents <= 0 || !OperationID.IsValid())
		return false;

	if (GetOwner()->HasAuthority())
		TableUpgradePicker->HandlePurchaseResult(OperationID, ExecuteTablePurchase(Station, OfferID, ExpectedPriceCents));
	else
		Server_PurchaseTable(Station, OfferID, ExpectedPriceCents, OperationID);
	return true;
}

EShopTablePurchaseResult UShopPlayerInteractionComponent::ExecuteTablePurchase(AShopTableUpgradeTerminal* Station,
	FName OfferID, int64 ExpectedPriceCents)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetNetMode() == NM_Client || !CanReach(Station))
		return EShopTablePurchaseResult::Unavailable;

	auto* Controller = Cast<APlayerController>(GetOwner());
	if (const auto* Pawn = Cast<APawn>(GetOwner())) Controller = Cast<APlayerController>(Pawn->GetController());
	if (!Controller) return EShopTablePurchaseResult::Unavailable;

	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	if (FindTerminalInView(Eye, View.Vector()) != Station) return EShopTablePurchaseResult::Unavailable;

	return UShopTableOfferComponent::Purchase(GetWorld(), OfferID, ExpectedPriceCents);
}

void UShopPlayerInteractionComponent::Server_PurchaseTable_Implementation(AShopTableUpgradeTerminal* Station, FName OfferID,
	int64 ExpectedPriceCents, FGuid OperationID)
{
	if (OperationID.IsValid()) Client_TablePurchaseResult(OperationID, ExecuteTablePurchase(Station, OfferID, ExpectedPriceCents));
}

void UShopPlayerInteractionComponent::Client_TablePurchaseResult_Implementation(FGuid OperationID, EShopTablePurchaseResult Result)
{
	if (IsTableUpgradePickerOpen()) TableUpgradePicker->HandlePurchaseResult(OperationID, Result);
}
