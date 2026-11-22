#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopATMComponent.h"
#include "Components/PrimitiveComponent.h"
#include "UI/ShopTerminalWidget.h"
#include "Props/InteractablePropManager.h"
#include "Resources/PressurizedColaContainer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Net/NetPushModelHelpers.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "WorldInteractionCarry.h"

UObject* WorldInteractionCarry::GetCarryReference(const APawn& Player, FName PropertyName)
{
	const auto* Property = FindFProperty<FObjectPropertyBase>(Player.GetClass(), PropertyName);
	return Property ? Property->GetObjectPropertyValue_InContainer(&Player) : nullptr;
}

bool WorldInteractionCarry::InvokeCarryRelease(APawn& Player, const TCHAR* FunctionName, UObject* HeldObject, EFunctionFlags NetRole)
{
	auto* Function = Player.FindFunction(FName(FunctionName));
	if (!Function || !Function->HasAnyFunctionFlags(NetRole) || !IsValid(HeldObject)) return false;
	for (TFieldIterator<FObjectPropertyBase> It(Function); It; ++It)
	{
		const FObjectPropertyBase* Property = *It;
		if (!Property->HasAnyPropertyFlags(CPF_Parm) || !HeldObject->IsA(Property->PropertyClass)) continue;
		FStructOnScope Params(Function);
		Property->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), HeldObject);
		Player.ProcessEvent(Function, Params.GetStructMemory());
		return true;
	}
	return false;
}

namespace
{
	APawn* GetCarryPawn(AActor* Owner)
	{
		if (auto* Controller = Cast<APlayerController>(Owner)) return Controller->GetPawn();
		return Cast<APawn>(Owner);
	}

	void MarkCarryPropertyDirty(APawn& Pawn, const FProperty& Property)
	{
		if (Pawn.HasAuthority() && Property.HasAnyPropertyFlags(CPF_Net))
			UNetPushModelHelpers::MarkPropertyDirty(&Pawn, Property.GetFName());
	}

	void ClearTakenItemFromHand(APawn& Pawn, const AActor* TakenActor, const UPrimitiveComponent* TakenComponent)
	{
		if (!TakenActor && !TakenComponent) return;
		auto ClearIfTaken = [&Pawn](FName Name, const UObject* Taken) -> UObject*
		{
			const auto* Property = FindFProperty<FObjectPropertyBase>(Pawn.GetClass(), Name);
			if (!Property) return nullptr;
			auto* Current = Property->GetObjectPropertyValue_InContainer(&Pawn);
			if (!Taken || Current != Taken) return Current;
			Property->SetObjectPropertyValue_InContainer(&Pawn, nullptr);
			MarkCarryPropertyDirty(Pawn, *Property);
			return nullptr;
		};

		const auto* RemainingActor = ClearIfTaken(TEXT("Holding Actor"), TakenActor);
		const auto* RemainingComponent = Cast<UPrimitiveComponent>(ClearIfTaken(TEXT("Holding Component"), TakenComponent));
		if (TakenActor && (!RemainingComponent || RemainingComponent->GetOwner() != TakenActor))
		{
			const auto* Ignore = FindFProperty<FArrayProperty>(Pawn.GetClass(), TEXT("trace  ignore"));
			if (const auto* Element = Ignore ? CastField<FObjectPropertyBase>(Ignore->Inner) : nullptr)
			{
				FScriptArrayHelper Entries(Ignore, Ignore->ContainerPtrToValuePtr<void>(&Pawn));
				const int32 OriginalCount = Entries.Num();
				for (int32 Index = OriginalCount - 1; Index >= 0; --Index)
					if (Element->GetObjectPropertyValue(Entries.GetRawPtr(Index)) == TakenActor) Entries.RemoveValues(Index, 1);
				if (Entries.Num() != OriginalCount) MarkCarryPropertyDirty(Pawn, *Ignore);
			}
		}

		// The Blueprint keeps its empty-hand mode separately from the carried references.
		if (!IsValid(RemainingActor) && !IsValid(RemainingComponent))
			if (const auto* State = FindFProperty<FByteProperty>(Pawn.GetClass(), TEXT("Holding State")); State && State->GetPropertyValue_InContainer(&Pawn) != 0)
			{
				State->SetPropertyValue_InContainer(&Pawn, 0);
				MarkCarryPropertyDirty(Pawn, *State);
			}
	}
}

bool WorldInteractionCarry::PublishHeldActor(APawn& Player, AActor& Item)
{
	const auto* HoldingActor = FindFProperty<FObjectPropertyBase>(Player.GetClass(), TEXT("Holding Actor"));
	if (!HoldingActor || !Item.IsA(HoldingActor->PropertyClass)
		|| IsValid(HoldingActor->GetObjectPropertyValue_InContainer(&Player))
		|| IsValid(GetCarryReference(Player, TEXT("Holding Component")))) return false;
	HoldingActor->SetObjectPropertyValue_InContainer(&Player, &Item);
	MarkCarryPropertyDirty(Player, *HoldingActor);
	if (const auto* State = FindFProperty<FByteProperty>(Player.GetClass(), TEXT("Holding State")))
	{
		State->SetPropertyValue_InContainer(&Player, 1);
		MarkCarryPropertyDirty(Player, *State);
	}
	const auto* TraceIgnore = FindFProperty<FArrayProperty>(Player.GetClass(), TEXT("trace  ignore"));
	if (const auto* Element = TraceIgnore ? CastField<FObjectPropertyBase>(TraceIgnore->Inner) : nullptr)
	{
		FScriptArrayHelper Entries(TraceIgnore, TraceIgnore->ContainerPtrToValuePtr<void>(&Player));
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
			if (Element->GetObjectPropertyValue(Entries.GetRawPtr(Index)) == &Item)
			{
				Player.ForceNetUpdate();
				return true;
			}
		Element->SetObjectPropertyValue(Entries.GetRawPtr(Entries.AddValue()), &Item);
		MarkCarryPropertyDirty(Player, *TraceIgnore);
	}
	Player.ForceNetUpdate();
	return true;
}

void WorldInteractionCarry::ClearHeldActor(APawn& Player, const AActor& Item)
{
	ClearTakenItemFromHand(Player, &Item, nullptr);
	Player.ForceNetUpdate();
}

void WorldInteractionCarry::ClearLocalMountedItem(AActor& Item)
{
	auto* World = Item.GetWorld();
	if (!World || World->GetNetMode() != NM_Client) return;
	const auto* MountedProp = Cast<AActivePropActor>(&Item);
	const auto IsMounted = [&Item, MountedProp](const AActor* Candidate)
	{
		if (Candidate == &Item) return true;
		const auto* PredictedProp = Cast<AActivePropActor>(Candidate);
		return MountedProp && MountedProp->ISID > 0 && PredictedProp && PredictedProp->ISID == MountedProp->ISID;
	};
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Controller = It->Get();
		auto* Pawn = Controller && Controller->IsLocalController() ? Controller->GetPawn().Get() : nullptr;
		if (!Pawn) continue;
		auto* HeldActor = Cast<AActor>(GetCarryReference(*Pawn, TEXT("Holding Actor")));
		auto* HeldComponent = Cast<UPrimitiveComponent>(GetCarryReference(*Pawn, TEXT("Holding Component")));
		if (HeldComponent && !IsMounted(HeldComponent->GetOwner())) HeldComponent = nullptr;
		AActor* TakenActor = &Item;
		if (IsMounted(HeldActor)) TakenActor = HeldActor;
		else if (HeldComponent) TakenActor = HeldComponent->GetOwner();
		ClearTakenItemFromHand(*Pawn, TakenActor, HeldComponent);
	}
}

UShopPlayerInteractionComponent::UShopPlayerInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UShopPlayerInteractionComponent::CompleteCarryHandoff(AActor* TakenActor, UPrimitiveComponent* TakenComponent)
{
	auto* Pawn = GetCarryPawn(GetOwner());
	if (!Pawn || !Pawn->HasAuthority() || GetNetMode() == NM_Client) return;
	ClearTakenItemFromHand(*Pawn, TakenActor, TakenComponent);
	Pawn->ForceNetUpdate();
}

void UShopPlayerInteractionComponent::RequestScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID)
{
	if (LocalSequence == MAX_int64) return;
	const int64 Sequence = ++LocalSequence;
	if (GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client)
		OnActionResult.Broadcast(Sequence, ExecuteScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence));
	else Server_ScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence);
}

void UShopPlayerInteractionComponent::Server_ScannerAction_Implementation(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID, int64 Sequence)
{
	const auto Result=ExecuteScannerAction(Scanner, ExpectedSession, Action, ISID, CaseID, Sequence);
	UE_LOG(LogTemp,Verbose,TEXT("SHOP_SCANNER_ACTION action=%d sequence=%lld result=%s reachable=%d owner=%s position=%s"),
		int32(Action),Sequence,*UEnum::GetValueAsString(Result),IsValid(Scanner) && CanReach(Scanner->GetOwner()),*GetNameSafe(GetOwner()),*GetOwner()->GetActorLocation().ToString());
	Client_ActionResult(Sequence,Result);
}

void UShopPlayerInteractionComponent::Client_ActionResult_Implementation(int64 Sequence, EShopCheckoutResult Result) { ShowProductionResult(Result); OnActionResult.Broadcast(Sequence, Result); }

bool UShopPlayerInteractionComponent::CanReach(const AActor* Station) const
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const auto* PC = Cast<APlayerController>(GetOwner())) Pawn = PC->GetPawn();
	const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !PC || PC->GetPawn() != Pawn || !IsValid(Station) || Station->GetWorld() != GetWorld()) return false;
	return FMath::IsFinite(MaxInteractionDistance) && MaxInteractionDistance > 0
		&& ShopInteractionGeometry::DistanceToStation(Station,Pawn->GetActorLocation())<=MaxInteractionDistance;
}

void UShopPlayerInteractionComponent::RequestATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Values)
{
	if (LocalSequence == MAX_int64 || Values.IsEmpty() || Values.Num() > 256) return;
	const int64 Sequence = ++LocalSequence; const FGuid ID = FGuid::NewGuid();
	if (GetOwner() && GetOwner()->HasAuthority() && GetOwner()->GetNetMode() != NM_Client) OnActionResult.Broadcast(Sequence, ExecuteATMWithdrawal(ATM, Values, ID, Sequence));
	else Server_ATMWithdrawal(ATM, Values, ID, Sequence);
}
void UShopPlayerInteractionComponent::Server_ATMWithdrawal_Implementation(UShopATMComponent* ATM, const TArray<int32>& Values, FGuid ID, int64 Sequence)
{
	Client_ActionResult(Sequence, ExecuteATMWithdrawal(ATM, Values, ID, Sequence));
}
EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteATMWithdrawal(UShopATMComponent* ATM, const TArray<int32>& Values, FGuid ID, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence <= 0 || Sequence <= LastServerSequence) return EShopCheckoutResult::OperationConflict;
	LastServerSequence = Sequence;
	if (!IsValid(ATM) || !CanReach(ATM->GetOwner()) || ATM->GetWorld() != GetWorld() || Values.IsEmpty() || Values.Num() > 256) return EShopCheckoutResult::Unavailable;
	return ATM->Withdraw(ID, Values);
}

void UShopPlayerInteractionComponent::Client_OpenTerminal_Implementation(AActor* Station) { OpenTerminal(Station); }

AActor* UShopPlayerInteractionComponent::FindTerminalInView(const FVector& ViewLocation, const FVector& ViewDirection, FHitResult* StationHit) const
{
	const APawn* Pawn=Cast<APawn>(GetOwner());
	if (const auto* PC=Cast<APlayerController>(GetOwner())) Pawn=PC->GetPawn();
	if (!Pawn || !GetWorld() || ViewLocation.ContainsNaN() || ViewDirection.ContainsNaN()
		|| !FMath::IsFinite(MaxInteractionDistance) || MaxInteractionDistance<=0) return nullptr;
	const FVector Direction=ViewDirection.GetSafeNormal();
	if (Direction.IsNearlyZero()) return nullptr;
	// Reach is measured from the pawn to the authored control. Include the camera
	// offset in the ray budget so a raised giant counter can actually be hit.
	const double TraceDistance=MaxInteractionDistance+FVector::Distance(ViewLocation,Pawn->GetActorLocation());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShopTerminalInteraction),false,Pawn);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit,ViewLocation,ViewLocation+Direction*TraceDistance,ECC_Visibility,Params)) return nullptr;
	AActor* Station=Hit.GetActor();
	if (!IsValid(Station) || (!Station->FindComponentByClass<UShopScannerComponent>()
		&& !Station->FindComponentByClass<UShopATMComponent>() && !Station->FindComponentByClass<UShopProductionStationComponent>())) return nullptr;
	if (!CanReach(Station)) return nullptr;
	if (StationHit) *StationHit = Hit;
	return Station;
}

bool UShopPlayerInteractionComponent::TryOpenTerminalFromView()
{
	CancelSocketUnplugHold();
	APawn* Pawn=Cast<APawn>(GetOwner());
	APlayerController* PC=Cast<APlayerController>(GetOwner());
	if (PC) Pawn=PC->GetPawn();
	else if (Pawn) PC=Cast<APlayerController>(Pawn->GetController());
	if (!Pawn || !PC || !PC->IsLocalController() || PC->GetPawn()!=Pawn) return false;
	const bool bCloseValve=PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	if (BeginSocketUnplugFromView()) return true;
	if (auto* Control=FindColaContainerWorldControl(*Pawn))
	{
		auto* Container=CastChecked<APressurizedColaContainer>(Control->GetOwner());
		if (Container->IsValveControl(Control))
		{
			if (bCloseValve)
			{
				RequestTurnColaContainerValve(Container,false);
				return true;
			}
			return false; // The cached valve hit is forwarded through World Interaction.
		}
		if (!IsCableGrabActive()) return false; // The legacy carried component or actor uses World Interaction to connect.
	}
	if (IsCableGrabActive()) { if (!TryConnectSupplyHoseFromView()) RequestCableRelease(); return true; }
	if (TryTurnColaContainerValveFromView(!bCloseValve)) return true;
	const auto* Trace = FindFProperty<FStructProperty>(Pawn->GetClass(), TEXT("Hit Result On tick"));
	if (Trace && Trace->Struct == FHitResult::StaticStruct()
		&& TryUseSocketMachineControl(*Pawn, *Trace->ContainerPtrToValuePtr<FHitResult>(Pawn))) return true;
	FVector Location; FRotator Rotation;
	PC->GetPlayerViewPoint(Location,Rotation);
	FHitResult Hit;
	AActor* Station=FindTerminalInView(Location,Rotation.Vector(),&Hit);
	if (!Station) return false;
	if (TryUseSocketMachineControl(*Pawn, Hit)) return true;
	// The terminal query uses the controller's simple collision, which can mask the lever
	// in front of the housing. Leave the Blueprint's world interaction enabled for the whole machine.
	const bool bWorldFillingDisplay = Station->IsA<ASocketColaFillingMachine>();
	OpenTerminal(Station);
	return !bWorldFillingDisplay && IsTerminalOpen();
}

bool UShopPlayerInteractionComponent::TryUseSocketMachineControl(APawn& Pawn, const FHitResult& Hit)
{
	auto* Machine = Cast<ASocketColaFillingMachine>(Hit.GetActor());
	auto* Control = Hit.GetComponent();
	if (!IsValid(Machine) || !Machine->bIsActive || Machine->ISID <= 0 || !Machine->IsSocketInteractionComponent(Control, false)
		|| !Machine->CanReachControl(&Pawn, Control)) return false;
	// These controls apply on press. The Blueprint continues to pair the lever's press and release.
	if (Pawn.HasAuthority()) Machine->BeginSocketInteraction(&Pawn, Control);
	else Server_UseSocketMachineControl(Machine->ISID, Control->GetFName());
	OpenTerminal(Machine);
	return true;
}

void UShopPlayerInteractionComponent::Server_UseSocketMachineControl_Implementation(int32 MachineISID, FName ControlName)
{
	auto* Pawn = GetCarryPawn(GetOwner());
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || MachineISID <= 0 || ControlName.IsNone()) return;
	const auto* Manager = APropManager::Get(this);
	auto* Machine = Manager ? Cast<ASocketColaFillingMachine>(Manager->GetActiveActorFromISID(MachineISID)) : nullptr;
	if (!IsValid(Machine) || Machine->GetWorld() != GetWorld() || Machine->ISID != MachineISID) return;
	auto* Control = Cast<UPrimitiveComponent>(Machine->GetDefaultSubobjectByName(ControlName));
	if (!Machine->IsSocketInteractionComponent(Control, false)) return;
	Machine->BeginSocketInteraction(Pawn, Control);
}

bool UShopPlayerInteractionComponent::IsTerminalOpen() const
{
	return IsValid(TerminalWidget) && TerminalWidget->IsInViewport();
}

void UShopPlayerInteractionComponent::OpenTerminal(AActor* Station)
{
	if (!IsValid(Station) || Station->GetWorld() != GetWorld() || !CanReach(Station)
		|| (!Station->FindComponentByClass<UShopScannerComponent>() && !Station->FindComponentByClass<UShopATMComponent>() && !Station->FindComponentByClass<UShopProductionStationComponent>())) return;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC) if (const auto* Pawn = Cast<APawn>(GetOwner())) PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;
	if (!PC->IsLocalController()) { if (GetOwner()->HasAuthority()) Client_OpenTerminal(Station); return; }
	// Local E and the server's interaction callback can both open this station.
	// Keep the live widget and its focus instead of flashing a replacement.
	if (IsTerminalOpen() && TerminalWidget->IsShowingStation(Station)) return;
	if (TerminalWidget) TerminalWidget->Close();
	TerminalWidget = CreateWidget<UShopTerminalWidget>(PC, UShopTerminalWidget::StaticClass());
	if (!TerminalWidget) return;
	TerminalWidget->Configure(this, Station);
	const bool bWorldFillingDisplay = Station->IsA<ASocketColaFillingMachine>();
	if (bWorldFillingDisplay) TerminalWidget->AddToPlayerScreen(50);
	else TerminalWidget->AddToViewport(50);
	if (Station->FindComponentByClass<UShopProductionStationComponent>())
	{
		TerminalWidget->SetPositionInViewport(FVector2D::ZeroVector,false);
		TerminalWidget->SetAnchorsInViewport(FAnchors(0,0,1,1)); TerminalWidget->SetAlignmentInViewport(FVector2D::ZeroVector);
		if (bWorldFillingDisplay) return;
		FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(TerminalWidget->TakeWidget()); Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode); PC->SetShowMouseCursor(true); return;
	}
	TerminalWidget->SetPositionInViewport(FVector2D::ZeroVector, false);
	// Position resets the viewport anchors; set the centered anchors afterward.
	TerminalWidget->SetAnchorsInViewport(FAnchors(.5f,.5f));
	TerminalWidget->SetAlignmentInViewport(FVector2D(.5,.5));
	FInputModeUIOnly Mode; Mode.SetWidgetToFocus(TerminalWidget->TakeWidget()); PC->SetInputMode(Mode); PC->SetShowMouseCursor(true);
}

EShopCheckoutResult UShopPlayerInteractionComponent::ExecuteScannerAction(UShopScannerComponent* Scanner, FGuid ExpectedSession, EShopScannerAction Action, int32 ISID, FGuid CaseID, int64 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || GetOwner()->GetNetMode() == NM_Client) return EShopCheckoutResult::NotAuthority;
	if (Sequence <= LastServerSequence || Sequence <= 0) return EShopCheckoutResult::OperationConflict;
	LastServerSequence = Sequence;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const auto* Controller = Cast<APlayerController>(GetOwner())) Pawn = Controller->GetPawn();
	const auto* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Controller || Controller->GetPawn() != Pawn || !IsValid(Scanner) || Scanner->GetWorld() != GetWorld() || !IsValid(Scanner->GetOwner())) return EShopCheckoutResult::InvalidRequest;
	if (!CanReach(Scanner->GetOwner())) return EShopCheckoutResult::Unavailable;
	const FShopScannerState State = Scanner->GetScannerState();
	if (!ExpectedSession.IsValid() || State.SessionID != ExpectedSession) return EShopCheckoutResult::OperationConflict;
	switch (Action)
	{
	case EShopScannerAction::Save: return Scanner->SaveProducts();
	case EShopScannerAction::RemoveItem: return Scanner->RemoveProduct(ISID);
	case EShopScannerAction::Clear: return Scanner->ClearCart();
	case EShopScannerAction::LockBill: return Scanner->BeginCheckout();
	case EShopScannerAction::Complete: return Scanner->CompleteCheckout();
	case EShopScannerAction::CashCompensation:
	case EShopScannerAction::ProvideReplacement:
	{
		APropManager* Manager = APropManager::Get(this);
		FShopExpiryCase Case;
		if (State.Phase != EShopScannerPhase::Completed || !Manager || !Manager->ShopCheckout || !Manager->ShopCheckout->GetExpiryCase(CaseID, Case)
			|| Case.CheckoutID != State.Checkout.CheckoutID) return EShopCheckoutResult::InvalidRequest;
		if (Action == EShopScannerAction::CashCompensation) return Manager->ShopCheckout->ChooseCashCompensation(CaseID);
		if (!Scanner->IsProductPhysicallyInside(ISID)) return EShopCheckoutResult::InvalidReplacement;
		return Manager->ShopCheckout->ProvideReplacement(CaseID, ISID);
	}
	default: return EShopCheckoutResult::InvalidRequest;
	}
}
