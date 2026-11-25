#include "Components/PropGrabInputComponent.h"

#include "Components/ASMComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Shop/PlayerUpgradesComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "../Shop/WorldInteractionCarry.h"

namespace
{
	enum class EHandItem : uint8 { Other, EmptyBottle, LoadedBottle, Cap, Seal };

	EHandItem Classify(const APropManager& Manager, int32 ID)
	{
		const auto* Record = Manager.FindISIDData(ID);
		const auto* Product = Record ? Manager.FindProductDefinition(Record->ProductID) : nullptr;
		if (!Product) return EHandItem::Other;
		if (Product->bIsBottleCap) return EHandItem::Cap;
		if (Manager.ShopSupplies && Manager.ShopSupplies->IsTemporarySealProduct(Record->ProductID)) return EHandItem::Seal;
		if (Product->ConsumableProfile.NumOfPortions <= 0
			|| (Manager.ShopSupplies && Manager.ShopSupplies->IsCylinderProduct(Record->ProductID))) return EHandItem::Other;
		if (Product->Category != EPropProductCategory::Bottle)
		{
			const auto* Active = Record->ActiveActor.Get();
			const auto Class = Active ? TSubclassOf<AActivePropActor>(Active->GetClass())
				: Manager.ASMComponent ? Manager.ASMComponent->GetEffectiveActivePropActorClass(Record->ProductID)
				: Product->CustomActiveActorClass;
			if (!Class || !Class->IsChildOf(AColaPropActor::StaticClass())) return EHandItem::Other;
		}
		const auto* Unique = Manager.FindUniqueData(ID);
		const float Level = Unique ? Unique->EventData.ContentLevel : Product->DefaultUniqueData.EventData.ContentLevel;
		return Level <= UE_SMALL_NUMBER ? EHandItem::EmptyBottle : EHandItem::LoadedBottle;
	}

	bool CanStack(EHandItem Type)
	{
		return Type == EHandItem::EmptyBottle || Type == EHandItem::Cap || Type == EHandItem::Seal;
	}

	bool IsBottle(EHandItem Type)
	{
		return Type == EHandItem::EmptyBottle || Type == EHandItem::LoadedBottle;
	}

	int32 Capacity(const APawn& Player, EHandItem Type)
	{
		switch (Type)
		{
		case EHandItem::EmptyBottle: return UPlayerUpgradesComponent::GetCarryCapacityFor(&Player, EPlayerUpgradeType::EmptyBottleHandling);
		case EHandItem::Cap: return UPlayerUpgradesComponent::GetCarryCapacityFor(&Player, EPlayerUpgradeType::CapHandling);
		case EHandItem::Seal: return UPlayerUpgradesComponent::GetCarryCapacityFor(&Player, EPlayerUpgradeType::SealHandling);
		case EHandItem::LoadedBottle: return UPlayerUpgradesComponent::LoadedBottleCarryLimit;
		default: return 1;
		}
	}

	bool IsHeldBy(const APropManager& Manager, const APawn& Player, const AActivePropActor& Item)
	{
		const auto* Record = Manager.FindISIDData(Item.ISID);
		return Record && Record->ActiveActor == &Item && Record->State == EPropState::Held
			&& Record->RuntimeData.InteractionOwner.Get() == &Player;
	}

	AActor* PrimaryItem(const APawn& Player)
	{
		return Cast<AActor>(WorldInteractionCarry::GetCarryReference(Player, TEXT("Holding Actor")));
	}
}

void UPropGrabInputComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UPropGrabInputComponent, AdditionalHeldActors, COND_OwnerOnly);
}

bool UPropGrabInputComponent::CanCarryProduct(const APropManager& Manager, int32 ItemID, bool bAllowBatch) const
{
	const auto* Player = Cast<APawn>(GetOwner());
	if (!Player || IsValid(WorldInteractionCarry::GetCarryReference(*Player, TEXT("Holding Component")))) return false;

	const EHandItem Incoming = Classify(Manager, ItemID);
	const AActor* Primary = PrimaryItem(*Player);
	if (IsValid(Primary))
	{
		const auto* PrimaryProp = Cast<AActivePropActor>(Primary);
		if (!PrimaryProp || (PrimaryProp->ISID != ItemID
			&& (Primary->GetAttachParentActor() != Player || !IsHeldBy(Manager, *Player, *PrimaryProp)))) return false;
	}
	TArray<AActor*> Attached;
	Player->GetAttachedActors(Attached);
	int32 Count = 0;
	for (const auto* Actor : Attached)
	{
		const auto* Item = Cast<AActivePropActor>(Actor);
		if (!IsValid(Item) || !IsHeldBy(Manager, *Player, *Item)) continue;
		if (!bAllowBatch || !CanStack(Incoming) || Item->ISID == ItemID || Classify(Manager, Item->ISID) != Incoming) return false;
		++Count;
	}
	return Count < Capacity(*Player, Incoming);
}

bool UPropGrabInputComponent::IsSameCarryBatch(const APropManager& Manager, const AActor& HeldActor, int32 ItemID) const
{
	const auto* Item = Cast<AActivePropActor>(&HeldActor);
	if (!Item || ItemID <= 0 || Item->ISID == ItemID) return false;
	const EHandItem Held = Classify(Manager, Item->ISID);
	const EHandItem Aimed = Classify(Manager, ItemID);
	return (IsBottle(Held) && IsBottle(Aimed)) || (CanStack(Held) && Held == Aimed);
}

bool UPropGrabInputComponent::AddToCarryBatch(ACharacter& Player, AActivePropActor& Actor)
{
	if (!IsValid(PrimaryItem(Player)) || Actor.GetAttachParentActor() != &Player) return false;
	AdditionalHeldActors.AddUnique(&Actor);
	WorldInteractionCarry::IgnoreHeldActor(Player, Actor);
	LayoutCarryBatch();
	Player.ForceNetUpdate();
	return true;
}

bool UPropGrabInputComponent::UsesBatchActor(const AActor& Actor) const
{
	const auto* Player = Cast<APawn>(GetOwner());
	return Player && !AdditionalHeldActors.IsEmpty()
		&& (PrimaryItem(*Player) == &Actor || AdditionalHeldActors.ContainsByPredicate([&](const AActivePropActor* Item)
			{ return Item == &Actor; }));
}

bool UPropGrabInputComponent::ShouldDropBatchedActor(const AActor& Actor) const
{
	if (UsesBatchActor(Actor)) return true;
	const auto* Player = Cast<APawn>(GetOwner());
	const auto* Item = Cast<AActivePropActor>(&Actor);
	const auto* Manager = Item ? APropManager::Get(this) : nullptr;
	if (!Player || !Manager) return false;
	const EHandItem Type = Classify(*Manager, Item->ISID);
	return CanStack(Type) && Capacity(*Player, Type) > 1;
}

void UPropGrabInputComponent::RestorePrimaryCarry(const AActor* Removed)
{
	auto* Player = Cast<APawn>(GetOwner());
	if (!Player || !Player->HasAuthority() || AdditionalHeldActors.IsEmpty()) return;
	const auto* Manager = APropManager::Get(this);
	if (!Manager) return;
	AdditionalHeldActors.RemoveAll([&](const AActivePropActor* Item)
		{ return !IsValid(Item) || Item == Removed || Item->GetAttachParentActor() != Player || !IsHeldBy(*Manager, *Player, *Item); });
	if (!IsValid(PrimaryItem(*Player)) && !AdditionalHeldActors.IsEmpty())
	{
		auto* Next = AdditionalHeldActors[0].Get();
		if (WorldInteractionCarry::PublishHeldActor(*Player, *Next))
		{
			AdditionalHeldActors.RemoveAt(0);
			Next->SetActorRelativeLocation(FVector::ZeroVector);
			Next->ForceNetUpdate();
		}
	}
	LayoutCarryBatch();
	Player->ForceNetUpdate();
}

void UPropGrabInputComponent::LayoutCarryBatch()
{
	const auto* Manager = APropManager::Get(this);
	if (!Manager) return;
	for (int32 Index = 0; Index < AdditionalHeldActors.Num(); ++Index)
	{
		auto* Item = AdditionalHeldActors[Index].Get();
		if (!IsValid(Item)) continue;
		const int32 Slot = Index + 1;
		const bool bBottle = IsBottle(Classify(*Manager, Item->ISID));
		const FVector Offset = bBottle ? FVector(0, 9 * Slot, 0) : FVector(0, 4 * (Slot % 4), 3 * (Slot / 4));
		Item->SetActorRelativeLocation(Offset);
		Item->ForceNetUpdate();
	}
}

void UPropGrabInputComponent::OnCarryControllerChanged(APawn*, AController* OldController, AController* NewController)
{
	if (!OldController || OldController == NewController) return;
	if (Travel.Sequence) DropTravelActor();
	bTravelKeyHeld = false;
	ReleaseCarryBatch();
}

void UPropGrabInputComponent::ReleaseCarryBatch()
{
	auto* Player = Cast<APawn>(GetOwner());
	auto* Manager = Player && Player->HasAuthority() && !AdditionalHeldActors.IsEmpty() ? APropManager::Get(this) : nullptr;
	if (!IsValid(Manager)) return;
	auto Held = MoveTemp(AdditionalHeldActors);
	AdditionalHeldActors.Reset();
	if (auto* Primary = Cast<AActivePropActor>(PrimaryItem(*Player))) Held.AddUnique(Primary);
	for (const auto& Entry : Held)
	{
		auto* Item = Entry.Get();
		if (IsValid(Item) && IsHeldBy(*Manager, *Player, *Item)) Manager->ReleaseGrab(Item->ISID, Player);
		if (IsValid(Item)) WorldInteractionCarry::ClearHeldActor(*Player, *Item);
	}
	Player->ForceNetUpdate();
}

void UPropGrabInputComponent::OnRep_AdditionalHeldActors()
{
	auto* Player = Cast<APawn>(GetOwner());
	if (!Player || Player->HasAuthority()) return;
	for (const auto& Old : ClientIgnoredHeldActors)
	{
		auto* Item = Old.Get();
		if (Item && Item != PrimaryItem(*Player) && !AdditionalHeldActors.Contains(Item))
			WorldInteractionCarry::ClearHeldActor(*Player, *Item);
	}
	ClientIgnoredHeldActors.Reset();
	for (const auto& Entry : AdditionalHeldActors)
	{
		auto* Item = Entry.Get();
		if (!IsValid(Item)) continue;
		WorldInteractionCarry::IgnoreHeldActor(*Player, *Item);
		ClientIgnoredHeldActors.Add(Item);
	}
}

void UPropGrabInputComponent::NotifyPrimaryCarryRemoved(const AActor& Actor)
{
	const auto* Player = Cast<APawn>(GetOwner());
	if (Player && Player->HasAuthority() && GetNetMode() != NM_Standalone)
		Client_ClearRemovedCarryActor(const_cast<AActor*>(&Actor));
}

void UPropGrabInputComponent::Client_ClearRemovedCarryActor_Implementation(AActor* Removed)
{
	auto* Player = Cast<APawn>(GetOwner());
	if (Player && !Player->HasAuthority() && IsValid(Removed))
		WorldInteractionCarry::ClearHeldActor(*Player, *Removed);
}

void UPropGrabInputComponent::Server_DropBatchedActor_Implementation(AActor* ExpectedPrimary)
{
	auto* Player = Cast<APawn>(GetOwner());
	auto* Actor = Cast<AActivePropActor>(ExpectedPrimary);
	auto* Manager = APropManager::Get(this);
	if (!Player || !Manager || !IsValid(Actor) || !Player->HasAuthority() || GetNetMode() == NM_Client
		|| Travel.Sequence || PrimaryItem(*Player) != Actor
		|| !IsHeldBy(*Manager, *Player, *Actor)) return;
	const bool bBatchRelease = UsesBatchActor(*Actor);
	if (Manager->ReleaseGrab(Actor->ISID, Player) && !bBatchRelease)
		WorldInteractionCarry::ClearHeldActor(*Player, *Actor);
}

bool UPropGrabInputComponent::CanFillHeldBottle(const APropManager& Manager, int32 BottleID)
{
	const auto* Record = Manager.FindISIDData(BottleID);
	const auto* Player = Record && Record->State == EPropState::Held ? Cast<APawn>(Record->RuntimeData.InteractionOwner.Get()) : nullptr;
	const auto* Grab = Player ? Player->FindComponentByClass<UPropGrabInputComponent>() : nullptr;
	if (!Grab || !IsBottle(Classify(Manager, BottleID))) return true;

	TArray<AActor*> Attached;
	Player->GetAttachedActors(Attached);
	for (const auto* Actor : Attached)
	{
		const auto* Item = Cast<AActivePropActor>(Actor);
		if (IsValid(Item) && Item->ISID != BottleID && IsHeldBy(Manager, *Player, *Item)
			&& IsBottle(Classify(Manager, Item->ISID))) return false;
	}
	const int32 TravelingID = Grab->Travel.ISID;
	return TravelingID <= 0 || TravelingID == BottleID || !IsBottle(Classify(Manager, TravelingID));
}
