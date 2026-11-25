#include "Shop/SocketColaFillingMachine.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopProductionStationComponent.h"
#include "WorldInteractionCarry.h"

using WorldInteractionCarry::GetCarryReference;

bool ASocketColaFillingMachine::IsSocketInteractionComponent(const UPrimitiveComponent* Component, bool bIncludeLever) const
{
	return IsValid(Component) && (Component == BottleInputVolume || Component == CapInputVolume || Component == SealInputVolume
		|| Component == AutoSealSwitch || (!bUseFlowController && (Component == ContentSelectorDial || (bIncludeLever && Component == Lever))));
}

bool ASocketColaFillingMachine::CanReachControl(const APawn* Player, const UPrimitiveComponent* Control) const
{
	const auto* Controller = IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
	const auto* Actions = IsValid(Player) ? Player->FindComponentByClass<UShopPlayerInteractionComponent>() : nullptr;
	return Controller && Controller->GetPawn() == Player && Actions && Player->GetWorld() == GetWorld()
		&& IsValid(Control) && FMath::IsFinite(Actions->MaxInteractionDistance) && Actions->MaxInteractionDistance > 0
		&& Control->Bounds.GetBox().ComputeSquaredDistanceToPoint(Player->GetActorLocation()) <= FMath::Square(Actions->MaxInteractionDistance);
}

void ASocketColaFillingMachine::BeginSocketInteraction(APawn* Player, UPrimitiveComponent* HittingComponent)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !bIsActive || bIsHeld || IsMachinePlacementActive() || ISID <= 0
		|| !IsSocketInteractionComponent(HittingComponent) || !CanReachControl(Player, HittingComponent)) return;

	FVector Eye;
	FRotator View;
	Player->GetActorEyesViewPoint(Eye, View);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FillingMachineControl), false, Player);
	TArray<AActor*> Attached;
	Player->GetAttachedActors(Attached);
	Query.AddIgnoredActors(Attached);
	if (HittingComponent == CapInputVolume && Production && Production->View.SeatedBottleISID > 0)
	{
		const auto* Manager = APropManager::Get(this);
		const auto* Bottle = Manager ? Manager->GetActiveActorFromISID(Production->View.SeatedBottleISID) : nullptr;
		if (Bottle && Bottle->IsSeatedInFiller()) Query.AddIgnoredActor(Bottle);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, HittingComponent->Bounds.Origin, ECC_Visibility, Query)
		&& Hit.GetActor() != this) return;

	if (HittingComponent == AutoSealSwitch)
	{
		Production->bAutoApplySealOnRelease = !Production->bAutoApplySealOnRelease;
		ForceNetUpdate();
		return;
	}
	if (HittingComponent == ContentSelectorDial)
	{
		if (!bWorldLeverPressed)
		{
			Production->RefreshInputs();
			const int32 NextPort = (Production->View.SelectedContentPort + 1) % Production->GetSupportedContentCount();
			Production->ManualEquipmentAction(EShopProductionAction::SelectContentPort, NextPort, 0, Player);
		}
	}
	else if (HittingComponent == Lever)
	{
		if (bWorldLeverPressed || Production->View.bLeverHeld) return;
		WorldLeverOperator = Player;
		bWorldLeverPressed = true;
		Production->RefreshInputs();
		Production->ManualEquipmentAction(EShopProductionAction::HoldFillingLever, Production->View.SeatedBottleISID, 0, Player);
		ForceNetUpdate();
	}
	else
	{
		LoadHeldSupply(Player, HittingComponent);
		return;
	}

	const auto* Controller = CastChecked<APlayerController>(Player->GetController());
	if (Controller->GetLocalPlayer() || Controller->GetNetConnection())
		Player->FindComponentByClass<UShopPlayerInteractionComponent>()->OpenTerminal(this);
}

void ASocketColaFillingMachine::EndSocketInteraction(APawn* Player)
{
	if (HasAuthority() && IsValid(Player) && WorldLeverOperator.Get() == Player) ReleaseWorldLever();
}

void ASocketColaFillingMachine::ReleaseWorldLever()
{
	if (!HasAuthority() || !bWorldLeverPressed) return;
	if (Production->View.bLeverHeld && Production->View.LeverOperator == WorldLeverOperator.Get()) Production->EndManualLever();
	WorldLeverOperator.Reset();
	bWorldLeverPressed = false;
	ForceNetUpdate();
}

void ASocketColaFillingMachine::LoadHeldSupply(APawn* Player, UPrimitiveComponent* Socket)
{
	auto* Manager = APropManager::Get(this);
	if (!Manager) return;

	auto* HeldComponent = Cast<UPrimitiveComponent>(GetCarryReference(*Player, TEXT("Holding Component")));
	auto* HeldActor = Cast<AActivePropActor>(GetCarryReference(*Player, TEXT("Holding Actor")));
	const int32 ComponentID = HeldComponent ? Manager->GetISID(HeldComponent) : 0;
	const int32 ID = ComponentID > 0 ? Manager->ResolveRootISID(ComponentID) : (HeldActor ? HeldActor->ISID : 0);
	const auto* Record = Manager->FindISIDData(ID);
	if (!Record || Record->State != EPropState::Held || Record->RuntimeData.InteractionOwner.Get() != Player) return;

	Production->RefreshInputs();
	auto* Volume = Cast<UBoxComponent>(Socket);
	if (!Volume) return;
	if (Volume == CapInputVolume)
	{
		if (!Production->CanLoadCap(ID, Player)
			|| Production->View.CapTrayCount + GetPendingInputCount(Volume) >= Production->GetCapTrayCapacity()) return;
	}
	else if (Volume == SealInputVolume)
	{
		if (!Production->CanLoadSeal(ID, Player)
			|| Production->View.SealTrayCount + GetPendingInputCount(Volume) >= Production->GetSealTrayCapacity()) return;
	}
	else if (!Production->CanSeatBottle(ID, Player)) return;

	UPrimitiveComponent* TakenComponent = ComponentID > 0 ? HeldComponent : nullptr;
	AActor* TakenActor = TakenComponent ? TakenComponent->GetOwner() : HeldActor;
	if (!BeginInputPlacement(*Manager, ID, *Volume)) return;
	Player->FindComponentByClass<UShopPlayerInteractionComponent>()->CompleteCarryHandoff(TakenActor, TakenComponent);
}
