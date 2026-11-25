#include "Shop/ShopPlayerInteractionComponent.h"

#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopBottleDispenser.h"
#include "Shop/SocketColaFillingMachine.h"
#include "TimerManager.h"
#include "UI/ShopBalanceWidget.h"
#include "UI/ShopBottleDispenserWidget.h"
#include "UI/ShopTerminalWidget.h"

void UShopPlayerInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Pawn=Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this,&UShopPlayerInteractionComponent::OnFillingPawnControllerChanged);
		BindFillingTableInput(Cast<APlayerController>(Pawn->GetController()));
	}
	else BindFillingTableInput(Cast<APlayerController>(GetOwner()));
}

void UShopPlayerInteractionComponent::OnFillingPawnControllerChanged(APawn*, AController*, AController* NewController)
{
	CleanupFlowLeverInput();
	UnbindFillingTableInput();
	BindFillingTableInput(Cast<APlayerController>(NewController));
}

void UShopPlayerInteractionComponent::BindFillingTableInput(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || FillingTableInput) return;
	FillingTableInput=NewObject<UInputComponent>(PC);
	FillingTableInput->RegisterComponent();
	FillingTableInput->BindKey(EKeys::M,IE_Pressed,this,&UShopPlayerInteractionComponent::PressMachinePlacementKey).bConsumeInput=false;
	FillingTableInput->BindKey(EKeys::M,IE_Released,this,&UShopPlayerInteractionComponent::ReleaseMachinePlacementKey).bConsumeInput=false;

	for (const FKey& Key:{EKeys::Four,EKeys::NumPadFour})
		FillingTableInput->BindKey(Key,IE_Pressed,this,&UShopPlayerInteractionComponent::ToggleFillingTableReadout).bConsumeInput=false;

	FillingTableInput->BindKey(EKeys::U,IE_Pressed,this,&UShopPlayerInteractionComponent::OpenAimedFillingMachineUpgrades).bConsumeInput=false;
	FillingTableInput->BindKey(EKeys::H,IE_Pressed,this,&UShopPlayerInteractionComponent::OpenPlayerUpgrades);

	auto Bind=[this](const FKey& Key, EShopProductionAction Action, bool bShift=false)
	{
		FInputKeyBinding Binding(FInputChord(Key,bShift,false,false,false),IE_Pressed);
		Binding.bConsumeInput=false;
		Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this,&UShopPlayerInteractionComponent::UseFillingTableControl,Action);
		FillingTableInput->KeyBindings.Add(MoveTemp(Binding));
	};
	Bind(EKeys::R,EShopProductionAction::FitOrTightenCap);
	Bind(EKeys::R,EShopProductionAction::OpenCap,true);
	Bind(EKeys::T,EShopProductionAction::ApplyTemporarySeal);
	Bind(EKeys::X,EShopProductionAction::StopFilling);
	Bind(EKeys::V,EShopProductionAction::ResumeFilling);
	PC->PushInputComponent(FillingTableInput);
	if (PC->GetLocalPlayer())
		GetWorld()->GetTimerManager().SetTimer(StationReadoutTimer,this,&UShopPlayerInteractionComponent::UpdateStationReadoutsFromView,.1f,true);
}

void UShopPlayerInteractionComponent::UnbindFillingTableInput()
{
	CloseTableUpgradePicker();
	CleanupMachinePlacement();
	if (auto* World=GetWorld()) World->GetTimerManager().ClearTimer(StationReadoutTimer);
	if (ShopBalanceWidget) ShopBalanceWidget->RemoveFromParent();
	ShopBalanceWidget = nullptr;
	CloseFlowReadout();
	CloseFillingTableReadout();
	CloseBottleDispenserReadout();
	if (TerminalWidget && TerminalWidget->IsUpgradeMenu())
	{
		TerminalWidget->Close();
		TerminalWidget=nullptr;
	}
	PinnedFillingTable.Reset();
	DismissedFillingTable.Reset();
	if (!FillingTableInput) return;
	if (auto* PC=Cast<APlayerController>(FillingTableInput->GetOwner())) PC->PopInputComponent(FillingTableInput);
	FillingTableInput->DestroyComponent();
	FillingTableInput=nullptr;
}

void UShopPlayerInteractionComponent::EnsureShopBalanceWidget(APlayerController& PC)
{
	if (ShopBalanceWidget && ShopBalanceWidget->HasWallet() && ShopBalanceWidget->IsInViewport()) return;
	if (ShopBalanceWidget) ShopBalanceWidget->RemoveFromParent();
	ShopBalanceWidget = nullptr;

	// A joining client may not have received the shared wallet's owner yet.
	TActorIterator<APropManager> Manager(GetWorld());
	if (!Manager || !IsValid(Manager->SharedWallet)) return;

	ShopBalanceWidget = CreateWidget<UShopBalanceWidget>(&PC, UShopBalanceWidget::StaticClass());
	if (!ShopBalanceWidget) return;
	ShopBalanceWidget->SetWallet(*Manager->SharedWallet);
	if (!ShopBalanceWidget->AddToPlayerScreen(10)) ShopBalanceWidget = nullptr;
}

void UShopPlayerInteractionComponent::UpdateBottleDispenserReadout(APlayerController& PC, const AShopBottleDispenser* Dispenser, bool bAimingAtLever)
{
	if (!Dispenser || !PC.GetLocalPlayer() || !PC.GetLocalPlayer()->ViewportClient)
	{
		CloseBottleDispenserReadout();
		return;
	}
	if (!BottleDispenserWidget)
	{
		BottleDispenserWidget = CreateWidget<UShopBottleDispenserWidget>(&PC, UShopBottleDispenserWidget::StaticClass());
		if (!BottleDispenserWidget || !BottleDispenserWidget->AddToPlayerScreen(10))
		{
			BottleDispenserWidget = nullptr;
			return;
		}
	}
	BottleDispenserWidget->UpdateReadout(Dispenser->GetDispensedSupplyLabel(), Dispenser->GetDispensePriceCents(),
		Dispenser->GetCooldownRemaining(), bAimingAtLever, Dispenser->IsDropAreaBlocked());
}

void UShopPlayerInteractionComponent::CloseBottleDispenserReadout()
{
	if (BottleDispenserWidget) BottleDispenserWidget->RemoveFromParent();
	BottleDispenserWidget = nullptr;
}

bool UShopPlayerInteractionComponent::IsFillingTableReachable(const ASocketColaFillingMachine* Table) const
{
	return IsValid(Table) && Table->bIsActive && !Table->bIsHeld && Table->Production
		&& !Table->GetPlacementOperator() && Table->Production->bManualLeverMode && CanReach(Table);
}

ASocketColaFillingMachine* UShopPlayerInteractionComponent::FindAimedFillingTable(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit) return nullptr;
	if (auto* Table=Cast<ASocketColaFillingMachine>(Hit.GetActor()))
		return IsFillingTableReachable(Table) ? Table : nullptr;

	const auto* Bottle=Cast<AColaPropActor>(Hit.GetActor());
	if (!IsValid(Bottle) || !Bottle->IsSeatedInFiller() || !GetWorld()) return nullptr;
	for (TActorIterator<ASocketColaFillingMachine> Table(GetWorld()); Table; ++Table)
	{
		if (IsFillingTableReachable(*Table) && Table->Production->View.SeatedBottleISID==Bottle->ISID)
			return *Table;
	}
	return nullptr;
}

ASocketColaFillingMachine* UShopPlayerInteractionComponent::FindNearbyFillingTable(bool bPreferAimedCapInput) const
{
	const auto* PC=FillingTableInput ? Cast<APlayerController>(FillingTableInput->GetOwner()) : nullptr;
	const APawn* Pawn=PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !GetWorld() || !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance<=0) return nullptr;

	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye,View);
	const FVector PawnLocation=Pawn->GetActorLocation();
	const double AimDistance=MaxInteractionDistance+FVector::Distance(Eye,PawnLocation);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FillingTableInput),false,Pawn);
	TArray<AActor*> HeldActors;
	Pawn->GetAttachedActors(HeldActors);
	Query.AddIgnoredActors(HeldActors);
	FHitResult Aim;
	GetWorld()->LineTraceSingleByChannel(Aim,Eye,Eye+View.Vector()*AimDistance,ECC_Visibility,Query);
	if (bPreferAimedCapInput)
	{
		// A directly aimed cap input is a station control in its own right.
		// The cradle-distance rule below applies to the nearby-table fallback.
		if (auto* AimedTable=Cast<ASocketColaFillingMachine>(Aim.GetActor()); AimedTable && Aim.GetComponent()==AimedTable->CapInputVolume)
			return IsFillingTableReachable(AimedTable) ? AimedTable : nullptr;
	}
	const auto* AimedProp=Cast<AActivePropActor>(Aim.GetActor());

	ASocketColaFillingMachine* NearbyTable=nullptr;
	const double ReachSquared=FMath::Square(double(MaxInteractionDistance));
	double ClosestSquared=ReachSquared;
	for (TActorIterator<ASocketColaFillingMachine> Table(GetWorld()); Table; ++Table)
	{
		if (!Table->BottleHolderPoint || !IsFillingTableReachable(*Table))
			continue;
		const FVector Holder=Table->BottleHolderPoint->GetComponentLocation();
		const double DistanceSquared=FVector::DistSquared(PawnLocation,Holder);
		if (DistanceSquared>ReachSquared) continue;
		const int32 BottleID=Table->Production->View.SeatedBottleISID;
		if (Aim.GetActor()==*Table || (BottleID>0 && AimedProp && AimedProp->ISID==BottleID)) return *Table;
		if (NearbyTable && DistanceSquared>=ClosestSquared) continue;

		FHitResult Obstruction;
		if (GetWorld()->LineTraceSingleByChannel(Obstruction,Eye,Holder,ECC_Visibility,Query) && Obstruction.GetActor()!=*Table)
		{
			const auto* Prop=Cast<AActivePropActor>(Obstruction.GetActor());
			if (!Prop || BottleID<=0 || Prop->ISID!=BottleID) continue;
		}
		NearbyTable=*Table;
		ClosestSquared=DistanceSquared;
	}
	return NearbyTable;
}

void UShopPlayerInteractionComponent::OpenAimedFillingMachineUpgrades()
{
	if ((IsTerminalOpen() && !TerminalWidget->IsWorldReadout()) || LocalPlacementMachine.IsValid() || GetLocalFlowLever()) return;
	const auto* PC=FillingTableInput ? Cast<APlayerController>(FillingTableInput->GetOwner()) : nullptr;
	const auto* Pawn=PC ? PC->GetPawn().Get() : nullptr;
	if (!Pawn || !PC->IsLocalController()) return;

	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye,View);
	FHitResult Hit;
	FindTerminalInView(Eye,View.Vector(),&Hit);
	auto* Table=FindAimedFillingTable(Hit);
	if (Table && Table->CanOpenUpgrades(Pawn,MaxInteractionDistance)) ShowTerminal(Table,true,true);
}

void UShopPlayerInteractionComponent::ToggleFillingTableReadout()
{
	if (IsTerminalOpen() && !TerminalWidget->IsWorldReadout()) return;
	if (auto* Table=FindNearbyFillingTable())
	{
		if (IsTerminalOpen() && TerminalWidget->IsShowingStation(Table))
		{
			PinnedFillingTable.Reset();
			DismissedFillingTable=Table;
			TerminalWidget->Close();
		}
		else OpenTerminal(Table);
	}
}

void UShopPlayerInteractionComponent::UseFillingTableControl(EShopProductionAction Action)
{
	if (IsTerminalOpen() && !TerminalWidget->IsWorldReadout()) return;
	const bool bPauseControl=Action==EShopProductionAction::StopFilling || Action==EShopProductionAction::ResumeFilling;
	if (bPauseControl)
	{
		if (auto* Flow=GetLocalFlowLever())
		{
			Server_SetFlowPaused(Flow,Action==EShopProductionAction::StopFilling);
			return;
		}
	}
	const bool bFitOrTightenCap=Action==EShopProductionAction::FitOrTightenCap;
	auto* Table=FindNearbyFillingTable(bFitOrTightenCap);
	if (!Table) return;
	if (bFitOrTightenCap) OpenTerminal(Table);
	auto* Station=Table->Production.Get();
	const auto& View=Station->View;
	if (!View.SessionID.IsValid() || View.SeatedBottleISID<=0) return;
	if (bPauseControl)
	{
		if (Table->bUseFlowController || !View.bLeverHeld || View.LeverOperator!=GetOwner()) return;
	}
	RequestProductionAction(Station,View.SessionID,Action,View.SeatedBottleISID);
}
