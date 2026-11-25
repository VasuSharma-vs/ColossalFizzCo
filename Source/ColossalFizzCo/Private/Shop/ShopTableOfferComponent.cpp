#include "Shop/ShopTableOfferComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Props/InteractablePropManager.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace
{
	APropManager* FindTableWalletManager(const UWorld& World)
	{
		for (TActorIterator<APropManager> It(&World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}
}

FName UShopTableOfferComponent::GetPersistentOfferID() const
{
	if (!OfferID.IsNone()) return OfferID;
	const AActor* Table = GetOwner();
	const UPackage* Package = Table->GetLevel()->GetOutermost();
	const FString MapName = UWorld::RemovePIEPrefix(Package->GetName());
	return FName(*(MapName + TEXT(":") + Table->GetName()));
}

FVector UShopTableOfferComponent::GetMapLocation() const
{
	// The opposing rack assemblies share a pivot, so use their visible mesh positions.
	const AActor* Table = GetOwner();
	FBox MeshBounds(ForceInit);
	for (const UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Table))
	{
		if (Mesh->IsRegistered() && Mesh->GetStaticMesh() && Mesh->IsVisible())
		{
			MeshBounds += Mesh->Bounds.GetBox();
		}
	}
	return MeshBounds.IsValid ? MeshBounds.GetCenter() : Table->GetActorLocation();
}

TMap<FName, const UShopTableOfferComponent*> UShopTableOfferComponent::CollectOffers(const UWorld& World)
{
	TMap<FName, const UShopTableOfferComponent*> Offers;
	for (TObjectIterator<UShopTableOfferComponent> It; It; ++It)
	{
		const UShopTableOfferComponent* Offer = *It;
		const AActor* Table = Offer->GetOwner();
		if (Offer->PriceCents <= 0 || !Offer->IsRegistered() || Offer->GetWorld() != &World
			|| !IsValid(Table) || !Table->GetLevel()) continue;

		const FName ID = Offer->GetPersistentOfferID();
		if (auto** Existing = Offers.Find(ID))
		{
			// Ambiguous offer IDs must never charge for an arbitrary table.
			*Existing = nullptr;
		}
		else
		{
			Offers.Add(ID, Offer);
		}
	}
	return Offers;
}

void UShopTableOfferComponent::GetOffers(const UWorld* World, TArray<FShopTableOffer>& OutOffers)
{
	OutOffers.Reset();
	if (!World) return;
	const APropManager* Manager = FindTableWalletManager(*World);
	for (const auto& Entry : CollectOffers(*World))
	{
		const UShopTableOfferComponent* Offer = Entry.Value;
		if (!Offer) continue;
		FShopTableOffer& Item = OutOffers.AddDefaulted_GetRef();
		Item.OfferID = Entry.Key;
		Item.DisplayName = Offer->DisplayName;
		Item.WorldLocation = Offer->GetMapLocation();
		Item.PriceCents = Offer->PriceCents;
		Item.bOwned = Manager && Manager->PurchasedTableOffers.Contains(Entry.Key);
	}
	OutOffers.Sort([](const FShopTableOffer& Left, const FShopTableOffer& Right)
	{
		return Left.OfferID.LexicalLess(Right.OfferID);
	});
	for (int32 Index = 0; Index < OutOffers.Num(); ++Index)
	{
		if (OutOffers[Index].DisplayName.IsEmpty())
		{
			OutOffers[Index].DisplayName = FText::Format(NSLOCTEXT("ShopTableOffer", "NumberedTable", "Table {0}"),
				FText::AsNumber(Index + 1));
		}
	}
}

EShopTablePurchaseResult UShopTableOfferComponent::Purchase(UWorld* World, FName OfferID, int64 ExpectedPriceCents)
{
	using EResult = EShopTablePurchaseResult;
	if (!World || World->GetNetMode() == NM_Client || OfferID.IsNone()) return EResult::Unavailable;
	APropManager* Manager = FindTableWalletManager(*World);
	if (!Manager || !Manager->HasAuthority() || !Manager->PropManagerReady || !Manager->SharedWallet)
	{
		return EResult::Unavailable;
	}

	const UShopTableOfferComponent* Offer = CollectOffers(*World).FindRef(OfferID);
	if (!Offer) return EResult::Unavailable;
	if (Manager->PurchasedTableOffers.Contains(OfferID)) return EResult::AlreadyOwned;
	if (ExpectedPriceCents != Offer->PriceCents) return EResult::PriceChanged;

	const auto Result = Manager->SharedWallet->CommitWithResources(FGuid::NewGuid(), -Offer->PriceCents,
		TEXT("TablePurchase"), [&]
		{
			Manager->PurchasedTableOffers.Add(OfferID);
			return true;
		});
	if (Result == EWalletOperationResult::Applied) return EResult::Purchased;
	if (Result == EWalletOperationResult::InsufficientFunds) return EResult::InsufficientFunds;
	return EResult::Unavailable;
}
