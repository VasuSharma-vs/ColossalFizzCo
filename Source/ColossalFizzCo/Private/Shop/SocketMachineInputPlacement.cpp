#include "Shop/SocketColaFillingMachine.h"

#include "Algo/Count.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProductionStationComponent.h"
#include "WorldInteractionCarry.h"

namespace
{
	FTransform AdvanceInputPose(const AActivePropActor& Item, const FTransform& Destination, float Dt, float Speed)
	{
		constexpr float TurnRate = UE_TWO_PI * 2.f;
		return FTransform(FMath::QInterpConstantTo(Item.GetActorQuat(), Destination.GetRotation(), Dt, TurnRate),
			FMath::VInterpConstantTo(Item.GetActorLocation(), Destination.GetLocation(), Dt, Speed), Destination.GetScale3D());
	}
}

int32 ASocketColaFillingMachine::GetPendingInputCount(const UBoxComponent* Socket) const
{
	return Algo::CountIf(InputPlacements, [Socket](const FSocketMachineInputPlacement& Entry) { return Entry.Socket.Get() == Socket; });
}

bool ASocketColaFillingMachine::IsBottleHolderReserved(int32 OtherThanISID) const
{
	return InputPlacements.ContainsByPredicate([this, OtherThanISID](const FSocketMachineInputPlacement& Entry)
		{ return Entry.Socket.Get() == BottleInputVolume.Get() && Entry.ISID != OtherThanISID; });
}

FTransform ASocketColaFillingMachine::GetInputPlacementDestination(const AActivePropActor& Item, const UBoxComponent& Socket, int32 Slot) const
{
	if (&Socket != BottleInputVolume.Get()) return Production->GetTraySupplyPose(Item, Socket, Slot);
	const auto* Holder = Production->BottleHolder.Get();
	return FTransform(Holder->GetComponentQuat(), Holder->GetComponentLocation(), Item.GetActorScale3D());
}

bool ASocketColaFillingMachine::BeginInputPlacement(APropManager& Manager, int32 ID, UBoxComponent& Socket)
{
	const auto* Equipment = Manager.FindUniqueData(ISID);
	if (!HasAuthority() || !bIsActive || bIsHeld || !FMath::IsFinite(InputPlacementSpeed) || InputPlacementSpeed <= 0
		|| !Equipment || InputPlacements.ContainsByPredicate([ID](const FSocketMachineInputPlacement& Entry)
			{ return Entry.ISID == ID; })) return false;
	const int32 Loaded = &Socket == CapInputVolume.Get() ? Equipment->EventData.LoadedCapISIDs.Num() : Equipment->EventData.LoadedSealISIDs.Num();
	const int32 Slot = Loaded + GetPendingInputCount(&Socket);

	AActivePropActor* Item = nullptr;
	if (!Manager.PromoteInstanceToASM(ID, EPropActivationReason::Interaction, true, Item)
		|| !IsValid(Item) || !Item->bIsActive || Item->ISID != ID || !Item->PropMesh || !Item->PropMesh->GetStaticMesh()) return false;
	auto* OldHolder = Item->InteractionOwner.Get();
	// Reuse the prop's held lock until it arrives, so overlapping volumes and other players can't claim it.
	if (!Manager.SetPropState(ID, EPropState::Held) || !Manager.SetGrabber(ID, this)) return false;
	if (IsValid(OldHolder)) Item->PropMesh->IgnoreActorWhenMoving(OldHolder, false);
	Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Item->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	auto& Placement = InputPlacements.AddDefaulted_GetRef();
	Placement.ISID = ID;
	Placement.Destination = GetInputPlacementDestination(*Item, Socket, Slot);
	Placement.Item = Item;
	Placement.Socket = &Socket;
	if (GetNetMode() != NM_Standalone)
	{
		Item->SetReplicates(true);
		Item->SetReplicateMovement(true);
		Item->FlushNetDormancy();
	}
	Item->ForceNetUpdate();
	ForceNetUpdate();
	return true;
}

void ASocketColaFillingMachine::UpdateInputPlacements(float DeltaSeconds)
{
	if (InputPlacements.IsEmpty()) return;
	auto* Manager = APropManager::Get(this);
	if (!Manager || !bIsActive || bIsHeld || !FMath::IsFinite(InputPlacementSpeed) || InputPlacementSpeed <= 0)
	{
		CancelInputPlacements();
		return;
	}
	int32 CapsAhead = 0;
	int32 SealsAhead = 0;
	bool bPlacedItem = false;
	for (int32 Index = 0; Index < InputPlacements.Num();)
	{
		const FSocketMachineInputPlacement Entry = InputPlacements[Index];
		auto* Item = Entry.Item.Get();
		const auto* Socket = Entry.Socket.Get();
		const auto* Record = Manager->FindISIDData(Entry.ISID);
		const auto* Equipment = Manager->FindUniqueData(ISID);
		const bool bReserved = Record && Item && Record->ActiveActor == Item && Record->State == EPropState::Held
			&& Record->RuntimeData.InteractionOwner.Get() == this;
		const bool bCap = Socket == CapInputVolume;
		const bool bSeal = Socket == SealInputVolume;
		const bool bCanPlace = bReserved && Equipment && IsValid(Socket) && Item->PropMesh && Item->PropMesh->GetStaticMesh()
			&& (bCap ? Production->CanLoadCap(Entry.ISID, this) : bSeal ? Production->CanLoadSeal(Entry.ISID, this)
				: Production->CanSeatBottle(Entry.ISID, this));
		if (!bCanPlace)
		{
			InputPlacements.RemoveAt(Index);
			if (bReserved) Manager->ReleaseGrab(Entry.ISID, this);
			ForceNetUpdate();
			continue;
		}

		const int32 Ahead = bCap ? CapsAhead : bSeal ? SealsAhead : 0;
		const int32 Loaded = bCap ? Equipment->EventData.LoadedCapISIDs.Num() : Equipment->EventData.LoadedSealISIDs.Num();
		const FTransform Destination = GetInputPlacementDestination(*Item, *Socket, Loaded + Ahead);
		InputPlacements[Index].Destination = Destination;
		const FTransform Next = AdvanceInputPose(*Item, Destination, DeltaSeconds, InputPlacementSpeed);
		Manager->UpdateHeldTransform(Entry.ISID, Next);

		if (!Ahead && Next.Equals(Destination, .001))
		{
			InputPlacements.RemoveAt(Index);
			ForceNetUpdate();
			if (Manager->ReleaseGrab(Entry.ISID, this))
			{
				if (bCap || bSeal) Production->RefreshSupplyTrays(bCap ? Entry.ISID : 0, bSeal ? Entry.ISID : 0);
				else Production->SeatBottle(Entry.ISID);
				bPlacedItem = true;
			}
			continue;
		}
		CapsAhead += bCap;
		SealsAhead += bSeal;
		++Index;
	}
	if (bPlacedItem) Production->RefreshInputs();
}

void ASocketColaFillingMachine::UpdateClientInputPlacements(float DeltaSeconds)
{
	if (InputPlacements.IsEmpty()) return;
	auto* Manager = APropManager::Get(this);
	if (!Manager) return;
	for (FSocketMachineInputPlacement& Entry : InputPlacements)
	{
		auto* Item = Manager->GetActiveActorFromISID(Entry.ISID);
		if (!IsValid(Item) || !Item->bIsActive || Item->ISID != Entry.ISID || Item->IsSeatedInFiller()
			|| Production->View.SeatedBottleISID == Entry.ISID) continue;
		const auto* Data = Manager->FindUniqueData(Entry.ISID);
		if (Data && Data->EventData.StoredInMachineISID > 0) continue;

		const bool bAttachedToHand = Cast<APawn>(Item->GetAttachParentActor()) != nullptr;
		if (bAttachedToHand) Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (Entry.Item.Get() != Item || bAttachedToHand)
		{
			WorldInteractionCarry::ClearLocalMountedItem(*Item);
			Entry.Item = Item;
		}
		// Client-promoted props can keep their own actor for the same ISID.
		// Drive those actors from the station's destination while network actors receive normal movement updates.
		if (Item->InteractionOwner == this && !Item->HasAuthority()) continue;
		if (Item->PropMesh && Item->PropMesh->IsSimulatingPhysics()) Item->PropMesh->SetSimulatePhysics(false);
		Item->SetActorTransform(AdvanceInputPose(*Item, Entry.Destination, DeltaSeconds, InputPlacementSpeed),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ASocketColaFillingMachine::CancelInputPlacements()
{
	if (InputPlacements.IsEmpty()) return;
	auto Placements = MoveTemp(InputPlacements);
	if (GetNetMode() == NM_Client) return;
	ForceNetUpdate();
	if (auto* Manager = APropManager::Get(this))
		for (const FSocketMachineInputPlacement& Entry : Placements)
			if (const auto* Record = Manager->FindISIDData(Entry.ISID);
				Record && Record->ActiveActor == Entry.Item.Get() && Record->RuntimeData.InteractionOwner.Get() == this)
				Manager->ReleaseGrab(Entry.ISID, this);
}
