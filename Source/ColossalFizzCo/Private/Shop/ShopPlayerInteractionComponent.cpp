#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopATMComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PropGrabInputComponent.h"
#include "UI/SettingsControls.h"
#include "UI/ShopTerminalWidget.h"
#include "Props/InteractablePropManager.h"
#include "Resources/PressurizedColaContainer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Shop/ColaFlowController.h"
#include "Shop/ShopBottleDispenser.h"
#include "Shop/ShopTableUpgradeTerminal.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Net/NetPushModelHelpers.h"
#include "Net/UnrealNetwork.h"
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
		const bool bRemovedPrimary = TakenActor && WorldInteractionCarry::GetCarryReference(Pawn, TEXT("Holding Actor")) == TakenActor;
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

		if (auto* Grab = Pawn.FindComponentByClass<UPropGrabInputComponent>(); Pawn.HasAuthority() && Grab)
		{
			if (bRemovedPrimary) Grab->NotifyPrimaryCarryRemoved(*TakenActor);
			Grab->RestorePrimaryCarry(TakenActor);
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
	IgnoreHeldActor(Player, Item);
	Player.ForceNetUpdate();
	return true;
}

void WorldInteractionCarry::IgnoreHeldActor(APawn& Player, AActor& Item)
{
	const auto* TraceIgnore = FindFProperty<FArrayProperty>(Player.GetClass(), TEXT("trace  ignore"));
	if (const auto* Element = TraceIgnore ? CastField<FObjectPropertyBase>(TraceIgnore->Inner) : nullptr)
	{
		FScriptArrayHelper Entries(TraceIgnore, TraceIgnore->ContainerPtrToValuePtr<void>(&Player));
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
			if (Element->GetObjectPropertyValue(Entries.GetRawPtr(Index)) == &Item)
				return;
		Element->SetObjectPropertyValue(Entries.GetRawPtr(Entries.AddValue()), &Item);
		MarkCarryPropertyDirty(Player, *TraceIgnore);
	}
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

void UShopPlayerInteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UShopPlayerInteractionComponent,ActiveFlowLever,COND_OwnerOnly);
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

void UShopPlayerInteractionComponent::Client_OpenTerminal_Implementation(AActor* Station)
{
	const auto* Pawn = GetCarryPawn(GetOwner());
	const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : Cast<APlayerController>(GetOwner());
	if (PC && PC->IsLocalController()) OpenTerminal(Station);
}

AActor* UShopPlayerInteractionComponent::FindTerminalInView(const FVector& ViewLocation, const FVector& ViewDirection, FHitResult* StationHit) const
{
	if (StationHit) *StationHit=FHitResult();
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
	TArray<AActor*> HeldActors;
	Pawn->GetAttachedActors(HeldActors);
	Params.AddIgnoredActors(HeldActors);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit,ViewLocation,ViewLocation+Direction*TraceDistance,ECC_Visibility,Params)) return nullptr;
	if (StationHit) *StationHit=Hit;
	AActor* Station=Hit.GetActor();
	if (!IsValid(Station) || (!Station->FindComponentByClass<UShopScannerComponent>()
		&& !Station->FindComponentByClass<UShopATMComponent>() && !Station->FindComponentByClass<UShopProductionStationComponent>()
		&& !Station->IsA<AColaFlowController>() && !Station->IsA<AShopTableUpgradeTerminal>())) return nullptr;
	if (!CanReach(Station)) return nullptr;
	return Station;
}

bool UShopPlayerInteractionComponent::TryOpenTerminalFromView()
{
	if (LocalPlacementMachine.IsValid()) return true;
	if (IsTableUpgradePickerOpen()) return true;
	if (IsTerminalOpen() && TerminalWidget->IsUpgradeMenu()) return true;
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
	FVector Location; FRotator Rotation;
	PC->GetPlayerViewPoint(Location,Rotation);
	FHitResult Hit;
	AActor* Station=FindTerminalInView(Location,Rotation.Vector(),&Hit);
	if (auto* TableTerminal = Cast<AShopTableUpgradeTerminal>(Station))
		return OpenTableUpgradePicker(*PC, *TableTerminal);
	bool bAimingAtDispenserLever = false;
	if (auto* Dispenser = FindAimedBottleDispenser(*Pawn, Hit, Location, Rotation.Vector(), bAimingAtDispenserLever);
		Dispenser && bAimingAtDispenserLever)
	{
		if (Pawn->HasAuthority()) Dispenser->TryDispense(Pawn, MaxInteractionDistance);
		else Server_UseBottleDispenser(Dispenser);
		return true;
	}
	if (auto* Flow=Cast<AColaFlowController>(Station))
	{
		if (TryUseSocketMachineControl(*Pawn,Hit)) return true;
		const bool bHitHousing=Hit.GetComponent()==Flow->Housing;
		// A simple housing collider can cover a handle. Test the visible mesh at
		// the same viewpoint instead of reusing a hit from the previous frame.
		FCollisionQueryParams Query(SCENE_QUERY_STAT(FlowControllerInteraction),true,Pawn);
		TArray<AActor*> HeldActors;
		Pawn->GetAttachedActors(HeldActors);
		Query.AddIgnoredActors(HeldActors);
		const double Distance=MaxInteractionDistance+FVector::Distance(Location,Pawn->GetActorLocation());
		if (GetWorld()->LineTraceSingleByChannel(Hit,Location,Location+Rotation.Vector()*Distance,ECC_Visibility,Query)
			&& Hit.GetActor()==Flow && TryUseSocketMachineControl(*Pawn,Hit)) return true;
		// The pulled lever can leave the original sight line over the housing.
		return bHitHousing && Flow->UsesLatchedLever() && Flow->IsLeverHeldBy(Pawn) && BeginFlowLeverInput(*Pawn,*Flow);
	}
	const auto* HitTable=Cast<ASocketColaFillingMachine>(Hit.GetActor());
	// Prefer the current cap input over the cached Blueprint control hit.
	if (HitTable && Hit.GetComponent()==HitTable->CapInputVolume && TryUseSocketMachineControl(*Pawn,Hit)) return true;
	const auto* ForegroundProp=HitTable ? nullptr : Cast<AActivePropActor>(Hit.GetActor());
	const bool bHitBottleTrigger=HitTable && Hit.GetComponent()==HitTable->BottleInputVolume;
	if (bHitBottleTrigger || (ForegroundProp && ForegroundProp->ISID>0))
	{
		const auto* Carried=Cast<AActivePropActor>(WorldInteractionCarry::GetCarryReference(*Pawn,TEXT("Holding Actor")));
		if (!Carried)
			if (const auto* Component=Cast<UPrimitiveComponent>(WorldInteractionCarry::GetCarryReference(*Pawn,TEXT("Holding Component"))))
				Carried=Cast<AActivePropActor>(Component->GetOwner());
		const auto* Product=bHitBottleTrigger && Carried ? Carried->ResolveProductDefinition() : nullptr;
		// Keep the empty cradle usable when holding a bottle. A seated bottle is
		// identified by the table view, which can arrive before the prop's own state.
		if (Carried && (ForegroundProp || (Product && Product->bIsBottleCap && HitTable->SupportsCapType(Product->DefaultCapType))))
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(FillingMachineCapInput),false,Pawn);
			TArray<AActor*> HeldActors;
			Pawn->GetAttachedActors(HeldActors);
			Query.AddIgnoredActors(HeldActors);
			Query.AddIgnoredActor(Carried);
			if (ForegroundProp) Query.AddIgnoredActor(ForegroundProp);
			else Query.AddIgnoredComponent(HitTable->BottleInputVolume.Get());
			const double Distance=MaxInteractionDistance+FVector::Distance(Location,Pawn->GetActorLocation());
			FHitResult CapHit;
			if (GetWorld()->LineTraceSingleByChannel(CapHit,Location,Location+Rotation.Vector()*Distance,ECC_Visibility,Query))
			{
				const auto* Table=Cast<ASocketColaFillingMachine>(CapHit.GetActor());
				if (Table && CapHit.GetComponent()==Table->CapInputVolume
					&& (ForegroundProp ? Table->Production && Table->Production->View.SeatedBottleISID==ForegroundProp->ISID
						: Table==HitTable)
					&& TryUseSocketMachineControl(*Pawn,CapHit)) return true;
			}
		}
	}
	const auto* Trace=FindFProperty<FStructProperty>(Pawn->GetClass(),TEXT("Hit Result On tick"));
	if (Trace && Trace->Struct==FHitResult::StaticStruct())
	{
		const auto& CachedHit=*Trace->ContainerPtrToValuePtr<FHitResult>(Pawn);
		const auto* HitActor=CachedHit.GetActor();
		if (HitActor && !HitActor->IsA<AColaFlowController>() && TryUseSocketMachineControl(*Pawn,CachedHit)) return true;
	}
	if (!Station) return false;
	if (TryUseSocketMachineControl(*Pawn, Hit)) return true;
	const auto* Table=Cast<ASocketColaFillingMachine>(Station);
	if (Table && Table->bUseFlowController) return false;
	// The terminal query uses the controller's simple collision, which can mask the lever
	// in front of the housing. Leave the Blueprint's world interaction enabled for the whole machine.
	OpenTerminal(Station);
	return !Table && IsTerminalOpen();
}

bool UShopPlayerInteractionComponent::TryUseSocketMachineControl(APawn& Pawn, const FHitResult& Hit)
{
	if (auto* Flow=Cast<AColaFlowController>(Hit.GetActor()))
	{
		if (Flow->UpgradeControl && Hit.GetComponent()==Flow->UpgradeControl)
		{
			ShowTerminal(Flow,true,true);
			return true;
		}
		if (Hit.GetComponent()==Flow->Lever) return BeginFlowLeverInput(Pawn,*Flow);
		if (Hit.GetComponent()!=Flow->SelectorDial) return false;
		if (Pawn.HasAuthority()) Flow->UseSelector(&Pawn,MaxInteractionDistance);
		else Server_UseFlowSelector(Flow);
		return true;
	}
	auto* Machine = Cast<ASocketColaFillingMachine>(Hit.GetActor());
	auto* Control = Hit.GetComponent();
	if (!IsValid(Machine) || !Machine->bIsActive || Machine->ISID <= 0 || !Machine->IsSocketInteractionComponent(Control, false)
		|| !Machine->CanReachControl(&Pawn, Control)) return false;
	// These controls apply on press. The Blueprint continues to pair the lever's press and release.
	if (Pawn.HasAuthority()) Machine->BeginSocketInteraction(&Pawn, Control);
	else Server_UseSocketMachineControl(Machine->ISID, Control->GetFName());
	if (!Machine->bUseFlowController) OpenTerminal(Machine);
	return true;
}

AShopBottleDispenser* UShopPlayerInteractionComponent::FindAimedBottleDispenser(const APawn& Pawn, const FHitResult& Hit,
	const FVector& Eye, const FVector& Direction, bool& bAimingAtLever) const
{
	bAimingAtLever = false;
	auto* Dispenser = Cast<AShopBottleDispenser>(Hit.GetActor());
	const FVector PawnLocation = Pawn.GetActorLocation();
	const double ReachSquared = FMath::Square(double(MaxInteractionDistance));
	if (!Dispenser || FVector::DistSquared(PawnLocation, Hit.ImpactPoint) > ReachSquared) return nullptr;
	bAimingAtLever = Dispenser->IsLeverComponent(Hit.GetComponent());
	if (!bAimingAtLever)
	{
		// The dispenser's simple housing hull can cover its visible lever.
		const double TraceDistance = MaxInteractionDistance + FVector::Distance(Eye, PawnLocation);
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(BottleDispenserAim), true, &Pawn);
		FHitResult LeverHit;
		bAimingAtLever = GetWorld()->LineTraceSingleByChannel(LeverHit, Eye, Eye + Direction * TraceDistance, ECC_Visibility, Query)
			&& LeverHit.GetActor() == Dispenser && Dispenser->IsLeverComponent(LeverHit.GetComponent())
			&& FVector::DistSquared(PawnLocation, LeverHit.ImpactPoint) <= ReachSquared;
	}
	return Dispenser;
}

void UShopPlayerInteractionComponent::Server_UseBottleDispenser_Implementation(AShopBottleDispenser* Dispenser)
{
	auto* Pawn = GetCarryPawn(GetOwner());
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || !IsValid(Dispenser) || Dispenser->GetWorld() != GetWorld()) return;
	Dispenser->TryDispense(Pawn, MaxInteractionDistance);
}

bool UShopPlayerInteractionComponent::BeginFlowLeverInput(APawn& Pawn, AColaFlowController& FlowController)
{
	if (PressedFlowLever.IsValid()) return true;
	auto* PC=Cast<APlayerController>(Pawn.GetController());
	if (!PC || !PC->IsLocalController()) return false;
	if (!FlowControlInput || FlowControlInput->GetOwner()!=PC)
	{
		if (FlowControlInput) CleanupFlowLeverInput();
		const auto* Interact=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/Actions/IA_Intract.IA_Intract"));
		if (!Interact) return false;
		auto* Input=NewObject<UEnhancedInputComponent>(PC);
		Input->RegisterComponent();
		Input->BindAction(Interact,ETriggerEvent::Completed,this,&UShopPlayerInteractionComponent::EndFlowLeverInput);
		Input->BindAction(Interact,ETriggerEvent::Canceled,this,&UShopPlayerInteractionComponent::EndFlowLeverInput);
		FlowControlInput=Input;
	}
	PressedFlowLever=&FlowController;
	PC->PushInputComponent(FlowControlInput);
	Server_SetFlowLever(&FlowController,true);
	return true;
}

void UShopPlayerInteractionComponent::EndFlowLeverInput()
{
	auto* Flow=PressedFlowLever.Get();
	PressedFlowLever.Reset();
	if (FlowControlInput)
		if (auto* PC=Cast<APlayerController>(FlowControlInput->GetOwner())) PC->PopInputComponent(FlowControlInput);
	if (Flow) Server_SetFlowLever(Flow,false);
	UpdateStationReadoutsFromView();
}

void UShopPlayerInteractionComponent::CleanupFlowLeverInput()
{
	PressedFlowLever.Reset();
	CloseFlowReadout();
	if (FlowControlInput)
	{
		if (auto* PC=Cast<APlayerController>(FlowControlInput->GetOwner())) PC->PopInputComponent(FlowControlInput);
		FlowControlInput->DestroyComponent();
		FlowControlInput=nullptr;
	}
	if (GetOwner()->HasAuthority())
	{
		if (IsValid(ActiveFlowLever)) ActiveFlowLever->EndLever(GetCarryPawn(GetOwner()));
		ActiveFlowLever=nullptr;
	}
}

AColaFlowController* UShopPlayerInteractionComponent::GetLocalFlowLever() const
{
	auto* Flow=ActiveFlowLever.Get();
	return IsValid(Flow) && Flow->IsLeverHeldBy(GetCarryPawn(GetOwner())) ? Flow : PressedFlowLever.Get();
}

void UShopPlayerInteractionComponent::UpdateStationReadoutsFromView()
{
	const auto* Pawn=GetCarryPawn(GetOwner());
	auto* PC=Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PC && PC->GetLocalPlayer()) EnsureShopBalanceWidget(*PC);
	if (!PC || !PC->GetLocalPlayer() || LocalPlacementMachine.IsValid() || IsTableUpgradePickerOpen()
		|| (IsTerminalOpen() && !TerminalWidget->IsWorldReadout()))
	{
		CloseFlowReadout();
		CloseFillingTableReadout();
		CloseBottleDispenserReadout();
		return;
	}

	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye,View);
	FHitResult Hit;
	auto* Station=FindTerminalInView(Eye,View.Vector(),&Hit);
	bool bAimingAtDispenserLever = false;
	const auto* Dispenser = FindAimedBottleDispenser(*Pawn, Hit, Eye, View.Vector(), bAimingAtDispenserLever);
	UpdateBottleDispenserReadout(*PC, Dispenser, bAimingAtDispenserLever);

	auto* Flow=GetLocalFlowLever();
	if (!Flow || !Flow->IsLeverHeldBy(Pawn) || !CanReach(Flow))
		Flow=Cast<AColaFlowController>(Station);
	if (Flow) ShowTerminal(Flow,false);
	else CloseFlowReadout();

	auto* AimedTable=FindAimedFillingTable(Hit);
	if (DismissedFillingTable.Get()!=AimedTable) DismissedFillingTable.Reset();
	if (!IsFillingTableReachable(PinnedFillingTable.Get())) PinnedFillingTable.Reset();
	auto* Table=AimedTable ? AimedTable : PinnedFillingTable.Get();
	if (Table && Table!=DismissedFillingTable.Get()) ShowTerminal(Table,false);
	else CloseFillingTableReadout();
}

void UShopPlayerInteractionComponent::CloseFlowReadout()
{
	if (!FlowReadoutWidget) return;
	FlowReadoutWidget->Close();
	FlowReadoutWidget=nullptr;
}

void UShopPlayerInteractionComponent::CloseFillingTableReadout()
{
	if (!TerminalWidget || !TerminalWidget->IsWorldReadout()) return;
	TerminalWidget->Close(false);
	TerminalWidget=nullptr;
}

void UShopPlayerInteractionComponent::RequestFlowControllerPause(bool bPaused)
{
	if (auto* Flow=GetLocalFlowLever()) Server_SetFlowPaused(Flow,bPaused);
}

void UShopPlayerInteractionComponent::Server_SetFlowLever_Implementation(AColaFlowController* Flow, bool bPressed)
{
	auto* Pawn=GetCarryPawn(GetOwner());
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || !IsValid(Flow) || Flow->GetWorld()!=GetWorld()) return;
	if (!bPressed)
	{
		if (ActiveFlowLever!=Flow) return;
		Flow->ReleaseLeverInput(Pawn);
	}
	else
	{
		const auto* Current=ActiveFlowLever.Get();
		if (IsValid(Current) && Current!=Flow && Current->IsLeverHeldBy(Pawn)) return;
		if (!Flow->BeginLever(Pawn,MaxInteractionDistance)) return;
	}
	ActiveFlowLever=Flow->IsLeverHeldBy(Pawn) ? Flow : nullptr;
	GetOwner()->ForceNetUpdate();
}

void UShopPlayerInteractionComponent::Server_SetFlowPaused_Implementation(AColaFlowController* Flow, bool bPaused)
{
	auto* Pawn=GetCarryPawn(GetOwner());
	if (IsValid(Pawn) && Pawn->HasAuthority() && IsValid(Flow) && ActiveFlowLever==Flow)
		Flow->SetFlowPaused(Pawn,bPaused);
}

bool UShopPlayerInteractionComponent::RequestFlowControllerUpgrade(AColaFlowController* Flow, EFlowControllerUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID)
{
	if (!IsValid(Flow) || Flow->GetWorld()!=GetWorld() || !OperationID.IsValid()) return false;
	Server_UpgradeFlowController(Flow,Upgrade,ExpectedRank,OperationID);
	return true;
}

void UShopPlayerInteractionComponent::Server_UpgradeFlowController_Implementation(AColaFlowController* Flow, EFlowControllerUpgradeType Upgrade,
	int32 ExpectedRank, FGuid OperationID)
{
	const auto* Pawn=GetCarryPawn(GetOwner());
	const auto Result=IsValid(Pawn) && Pawn->HasAuthority() && CanReach(Flow)
		? Flow->PurchaseUpgrade(Pawn,MaxInteractionDistance,Upgrade,ExpectedRank,OperationID)
		: EFlowControllerPurchaseResult::Unavailable;
	Client_FlowControllerUpgradeResult(Flow,OperationID,Result);
}

void UShopPlayerInteractionComponent::Client_FlowControllerUpgradeResult_Implementation(AColaFlowController*, FGuid OperationID,
	EFlowControllerPurchaseResult Result)
{
	if (IsTerminalOpen() && TerminalWidget->IsFlowUpgradeMenu())
		TerminalWidget->FlowUpgradePurchaseResult(OperationID,Result);
}

bool UShopPlayerInteractionComponent::RequestFillingMachineUpgrade(ASocketColaFillingMachine* Table, EFillingMachineUpgradeType Upgrade,
	int32 ExpectedRank, FGuid OperationID)
{
	if (!IsFillingTableReachable(Table) || !OperationID.IsValid()) return false;
	Server_UpgradeFillingMachine(Table,Upgrade,ExpectedRank,OperationID);
	return true;
}

void UShopPlayerInteractionComponent::Server_UpgradeFillingMachine_Implementation(ASocketColaFillingMachine* Table,
	EFillingMachineUpgradeType Upgrade, int32 ExpectedRank, FGuid OperationID)
{
	const auto* Pawn=GetCarryPawn(GetOwner());
	const auto Result=IsValid(Pawn) && Pawn->HasAuthority() && IsFillingTableReachable(Table)
		? Table->PurchaseUpgrade(Pawn,MaxInteractionDistance,Upgrade,ExpectedRank,OperationID)
		: EFillingMachinePurchaseResult::Unavailable;
	Client_FillingMachineUpgradeResult(OperationID,Result);
}

void UShopPlayerInteractionComponent::Client_FillingMachineUpgradeResult_Implementation(FGuid OperationID, EFillingMachinePurchaseResult Result)
{
	if (IsTerminalOpen() && TerminalWidget->IsFillingUpgradeMenu())
		TerminalWidget->FillingUpgradePurchaseResult(OperationID,Result);
}

void UShopPlayerInteractionComponent::Server_UseFlowSelector_Implementation(AColaFlowController* FlowController)
{
	auto* Pawn=GetCarryPawn(GetOwner());
	if (IsValid(Pawn) && Pawn->HasAuthority() && IsValid(FlowController) && FlowController->GetWorld()==GetWorld())
		FlowController->UseSelector(Pawn,MaxInteractionDistance);
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
	ShowTerminal(Station,true);
}

void UShopPlayerInteractionComponent::OpenPlayerUpgrades()
{
	if (IsTableUpgradePickerOpen() || (IsTerminalOpen() && !TerminalWidget->IsWorldReadout())
		|| LocalPlacementMachine.IsValid() || GetLocalFlowLever()) return;
	const auto* Pawn = GetCarryPawn(GetOwner());
	auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->GetLocalPlayer() || PC->GetPawn() != Pawn) return;
	// The input mode change must not swallow a release needed by an active interaction.
	const auto* LocalPlayer=PC->GetLocalPlayer();
	if (PC->IsInputKeyDown(SettingsControls::GrabKey(LocalPlayer)) || PC->IsInputKeyDown(SettingsControls::InteractKey(LocalPlayer))) return;

	CloseFlowReadout();
	CloseFillingTableReadout();
	CloseBottleDispenserReadout();
	TerminalWidget = CreateWidget<UShopTerminalWidget>(PC, UShopTerminalWidget::StaticClass());
	if (!TerminalWidget) return;
	TerminalWidget->ConfigurePlayerUpgrades(this);
	if (!TerminalWidget->AddToPlayerScreen(50))
	{
		TerminalWidget = nullptr;
		return;
	}
	TerminalWidget->SetPositionInViewport(FVector2D::ZeroVector, false);
	TerminalWidget->SetAnchorsInViewport(FAnchors(0,0,1,1));
	TerminalWidget->SetAlignmentInViewport(FVector2D::ZeroVector);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(TerminalWidget->TakeWidget());
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
}

void UShopPlayerInteractionComponent::ShowTerminal(AActor* Station, bool bManualOpen, bool bOpenUpgrades)
{
	if (IsTableUpgradePickerOpen() || !IsValid(Station) || Station->GetWorld()!=GetWorld() || !CanReach(Station)) return;
	const bool bFlowController=Station->IsA<AColaFlowController>();
	const bool bFlowReadout=bFlowController && !bOpenUpgrades;
	const bool bProduction=Station->FindComponentByClass<UShopProductionStationComponent>()!=nullptr;
	if (!bFlowController && !bProduction && !Station->FindComponentByClass<UShopScannerComponent>() && !Station->FindComponentByClass<UShopATMComponent>()) return;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC) if (const auto* Pawn = Cast<APawn>(GetOwner())) PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;
	if (!PC->IsLocalController()) { if (GetOwner()->HasAuthority()) Client_OpenTerminal(Station); return; }
	if (IsTerminalOpen() && TerminalWidget->IsPlayerUpgradeMenu()) return;
	if (bManualOpen && !bFlowController && !bOpenUpgrades)
	{
		PinnedFillingTable=Cast<ASocketColaFillingMachine>(Station);
		DismissedFillingTable.Reset();
	}
	// Local E and the server's interaction callback can both open this station.
	// Keep the live widget and its focus instead of flashing a replacement.
	if (bOpenUpgrades) CloseFlowReadout();
	auto& Widget=bFlowReadout ? FlowReadoutWidget : TerminalWidget;
	if (Widget && Widget->IsInViewport() && Widget->IsShowingStation(Station) && Widget->IsUpgradeMenu()==bOpenUpgrades) return;
	if (Widget) Widget->Close(bManualOpen && !bOpenUpgrades);
	Widget=CreateWidget<UShopTerminalWidget>(PC,UShopTerminalWidget::StaticClass());
	if (!Widget) return;
	Widget->Configure(this,Station,bOpenUpgrades);
	const bool bPassive=Widget->IsWorldReadout();
	if (bPassive || bOpenUpgrades)
	{
		if (!Widget->AddToPlayerScreen(50)) { Widget=nullptr; return; }
	}
	else Widget->AddToViewport(50);
	Widget->SetPositionInViewport(FVector2D::ZeroVector,false);
	if (bPassive || bProduction || bOpenUpgrades)
	{
		Widget->SetAnchorsInViewport(FAnchors(0,0,1,1));
		Widget->SetAlignmentInViewport(FVector2D::ZeroVector);
		if (bPassive) return;
		if (!bOpenUpgrades)
		{
			FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(Widget->TakeWidget()); Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode); PC->SetShowMouseCursor(true); return;
		}
	}
	else
	{
		// Position resets the viewport anchors; set the centered anchors afterward.
		Widget->SetAnchorsInViewport(FAnchors(.5f,.5f));
		Widget->SetAlignmentInViewport(FVector2D(.5,.5));
	}
	FInputModeUIOnly Mode; Mode.SetWidgetToFocus(Widget->TakeWidget()); PC->SetInputMode(Mode); PC->SetShowMouseCursor(true);
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
