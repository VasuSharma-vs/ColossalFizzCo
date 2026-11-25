#include "Shop/SocketColaFillingMachine.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProductionStationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogMachineRelocation, Log, All);

namespace ManualEquipment { FPropEventSyncDelta State(const FPropEventUpdateData& Event, int32 Count); }

namespace
{
	bool CanRelocateWithBottle(const UShopProductionStationComponent& Production, const AColaPropActor* Bottle)
	{
		return IsValid(Bottle) && Production.IsBottleAligned(Bottle->ISID)
			&& Bottle->IsManufacturingSafeToRelease() && !Bottle->IsManufacturingFlowActive();
	}

	bool ClearSavedSupplyConnections(APropManager& Manager, int32 MachineISID, int32 PortCount,
		const FPropEventUpdateData& Equipment)
	{
		TSet<int32> Sources;
		for (int32 SourceISID : Equipment.ConnectedSupplyISIDs)
			if (SourceISID > 0) Sources.Add(SourceISID);
		if (Equipment.LinkedSupplyISID > 0) Sources.Add(Equipment.LinkedSupplyISID);
		if (Sources.IsEmpty()) return true;

		auto EquipmentUpdate = ManualEquipment::State(Equipment, PortCount);
		EquipmentUpdate.ISID = MachineISID;
		EquipmentUpdate.ConnectedSupplyISIDs.Init(0, PortCount);
		EquipmentUpdate.LinkedSupplyISID = 0;
		TArray<FPropEventSyncDelta> Updates{EquipmentUpdate};
		for (int32 SourceISID : Sources)
		{
			const auto* Source = Manager.FindUniqueData(SourceISID);
			if (SourceISID == MachineISID || !Source || Source->EventData.StoredInMachineISID != MachineISID) continue;
			auto SourceUpdate = ManualEquipment::State(Source->EventData, 1);
			SourceUpdate.ISID = SourceISID;
			SourceUpdate.StoredInMachineISID = 0;
			Updates.Add(SourceUpdate);
		}
		return Manager.SubmitAtomicInstanceEventUpdates(Updates);
	}
}

bool ASocketColaFillingMachine::BeginMachinePlacement(APawn& Player)
{
	const AController* Controller = Player.GetController();
	if (!HasAuthority() || GetNetMode() == NM_Client || !IsValid(Controller) || Controller->GetPawn() != &Player
		|| Player.GetWorld() != GetWorld() || bMachinePlacementActive || !bIsActive || bIsHeld || bIsInMotion
		|| !IsValid(PropMesh) || PropMesh->IsSimulatingPhysics() || !Production || !BottleFiller
		|| bWorldLeverPressed || Production->View.bLeverHeld || BottleFiller->bIsFilling || !InputPlacements.IsEmpty())
	{
		UE_LOG(LogMachineRelocation, Verbose, TEXT("Cannot move %s in its current state (active %d, held %d, moving %d, simulating %d, lever %d, filling %d, inputs %d)"),
			*GetName(), bIsActive, bIsHeld, bIsInMotion, PropMesh && PropMesh->IsSimulatingPhysics(),
			bWorldLeverPressed || (Production && Production->View.bLeverHeld), BottleFiller && BottleFiller->bIsFilling, InputPlacements.Num());
		return false;
	}

	auto* Manager = APropManager::Get(this);
	const auto* Record = Manager ? Manager->FindISIDData(ISID) : nullptr;
	const auto* Equipment = Manager ? Manager->FindUniqueData(ISID) : nullptr;
	if (!Record || Record->ActiveActor != this || !Equipment)
	{
		UE_LOG(LogMachineRelocation, Verbose, TEXT("Cannot move %s without its managed prop state (instance %d)"), *GetName(), ISID);
		return false;
	}

	const int32 BottleISID = Production->View.SeatedBottleISID;
	const auto* Bottle = Cast<AColaPropActor>(Manager->GetActiveActorFromISID(BottleISID));
	if (BottleISID > 0 && !CanRelocateWithBottle(*Production, Bottle)) return false;

	TArray<int32> ItemIDs = Equipment->EventData.LoadedCapISIDs;
	for (int32 SealISID : Equipment->EventData.LoadedSealISIDs) ItemIDs.AddUnique(SealISID);
	if (Bottle) ItemIDs.AddUnique(BottleISID);
	if (!ClearSavedSupplyConnections(*Manager, ISID, GetActiveHosePortCount(), Equipment->EventData)) return false;
	for (int32 PortIndex = 0; PortIndex < GetActiveHosePortCount(); ++PortIndex) DisconnectSupplyNozzle(PortIndex);

	PlacementItems.Reset();
	for (int32 ItemISID : ItemIDs)
		if (const auto* Item = Manager->GetActiveActorFromISID(ItemISID); IsValid(Item) && Item != this)
			PlacementItems.Add({ItemISID, Item->IsHidden(), Item->GetActorEnableCollision()});
	PlacementBottleISID = BottleISID;
	if (Bottle) PlacementBottleRelativeTransform = Bottle->GetActorTransform().GetRelativeTransform(GetActorTransform());
	bPlacementWasHidden = IsHidden();
	bPlacementHadCollision = GetActorEnableCollision();
	PlacementOperator = &Player;
	bMachinePlacementActive = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	ApplyPlacementItems();
	FlushNetDormancy();
	ForceNetUpdate();
	return true;
}

bool ASocketColaFillingMachine::FinishMachinePlacement(APawn& Player, const FTransform& Pose)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !bIsActive || !Production || !bMachinePlacementActive || PlacementOperator != &Player
		|| !Pose.IsValid() || !GetActorScale3D().Equals(Pose.GetScale3D())) return false;
	auto* Manager = APropManager::Get(this);
	const auto* Record = Manager ? Manager->FindISIDData(ISID) : nullptr;
	if (!Record || Record->ActiveActor != this || Production->View.SeatedBottleISID != PlacementBottleISID) return false;
	if (PlacementBottleISID > 0 && !CanRelocateWithBottle(*Production,
		Cast<AColaPropActor>(Manager->GetActiveActorFromISID(PlacementBottleISID)))) return false;

	const FTransform OriginalPose = GetActorTransform();
	if (!Manager->InitializeInstanceTransform(ISID, Pose)) return false;
	if (!GetActorTransform().Equals(Pose, .1f))
	{
		Manager->InitializeInstanceTransform(ISID, OriginalPose);
		return false;
	}
	if (PlacementBottleISID > 0 && !Manager->InitializeInstanceTransform(PlacementBottleISID, PlacementBottleRelativeTransform * Pose))
	{
		Manager->InitializeInstanceTransform(ISID, OriginalPose);
		return false;
	}
	Manager->MarkInstanceDirty(ISID, EPropDirtyFlags::Transform);
	for (const auto& Item : PlacementItems)
		if (auto* ItemRecord = Manager->FindISIDData(Item.ISID); ItemRecord && ItemRecord->ActiveActor)
		{
			ItemRecord->ActiveTransform = ItemRecord->ActiveActor->GetActorTransform();
			Manager->MarkInstanceDirty(Item.ISID, EPropDirtyFlags::Transform);
		}
	ResetControlCable(this, ISID, ProductID, Pose);
	RestoreMachinePlacement();
	Production->RefreshInputs();
	return true;
}

void ASocketColaFillingMachine::CancelMachinePlacement(APawn& Player)
{
	if (HasAuthority() && GetNetMode() != NM_Client && PlacementOperator == &Player) RestoreMachinePlacement();
}

void ASocketColaFillingMachine::UpdateMachinePlacement()
{
	if (!bMachinePlacementActive) return;
	if (HasAuthority() && GetNetMode() != NM_Client)
	{
		const auto* Controller = IsValid(PlacementOperator) ? PlacementOperator->GetController() : nullptr;
		if (!IsValid(Controller) || Controller->GetPawn() != PlacementOperator || !bIsActive)
			RestoreMachinePlacement();
	}
	else ApplyPlacementItems();
}

void ASocketColaFillingMachine::RestoreMachinePlacement()
{
	if (!bMachinePlacementActive) return;
	bMachinePlacementActive = false;
	PlacementOperator = nullptr;
	PlacementItems.Reset();
	PlacementBottleISID = 0;
	SetActorEnableCollision(bPlacementHadCollision);
	SetActorHiddenInGame(bPlacementWasHidden);
	RestorePlacementItems();
	ForceNetUpdate();
}

void ASocketColaFillingMachine::OnRep_PlacementOperator()
{
	SetActorEnableCollision(!bMachinePlacementActive && bPlacementHadCollision);
	if (bMachinePlacementActive) ApplyPlacementItems();
	else
	{
		ResetControlCable(this, ISID, ProductID, GetActorTransform());
		RestorePlacementItems();
	}
}

void ASocketColaFillingMachine::ApplyPlacementItems()
{
	const auto* Manager = APropManager::Get(this);
	if (!Manager) return;
	for (const auto& ItemState : PlacementItems)
	{
		auto* Item = Manager->GetActiveActorFromISID(ItemState.ISID);
		if (!IsValid(Item) || Item == this || Item->ISID != ItemState.ISID) continue;
		if (!AppliedPlacementItems.ContainsByPredicate([Item](const auto& Applied) { return Applied.Actor.Get() == Item; }))
			AppliedPlacementItems.Add({Item, ItemState});
		if (!Item->IsHidden()) Item->SetActorHiddenInGame(true);
		if (Item->GetActorEnableCollision()) Item->SetActorEnableCollision(false);
		if (HasAuthority() && GetNetMode() != NM_Client) Item->ForceNetUpdate();
	}
}

void ASocketColaFillingMachine::RestorePlacementItems()
{
	for (const auto& Applied : AppliedPlacementItems)
		if (auto* Item = Applied.Actor.Get(); IsValid(Item) && Item->ISID == Applied.State.ISID)
		{
			Item->SetActorEnableCollision(Applied.State.bHadCollision);
			Item->SetActorHiddenInGame(Applied.State.bWasHidden);
			if (HasAuthority() && GetNetMode() != NM_Client) Item->ForceNetUpdate();
		}
	AppliedPlacementItems.Reset();
}
