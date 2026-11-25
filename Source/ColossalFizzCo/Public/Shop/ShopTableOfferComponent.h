#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopTableOfferComponent.generated.h"

UENUM()
enum class EShopTablePurchaseResult : uint8
{
	Purchased,
	AlreadyOwned,
	InsufficientFunds,
	PriceChanged,
	Unavailable
};

struct COLOSSALFIZZCO_API FShopTableOffer
{
	FName OfferID;
	FText DisplayName;
	FVector WorldLocation = FVector::ZeroVector;
	int64 PriceCents = 0;
	bool bOwned = false;
};

/** Marks a placed table as an offer on the shop floor plan. */
UCLASS(ClassGroup=(Shop), meta=(BlueprintSpawnableComponent, DisplayName="Shop Table Offer"))
class COLOSSALFIZZCO_API UShopTableOfferComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Set a positive amount to list the table. For example, 1250 is $12.50. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shop Table", meta=(ClampMin="0"))
	int64 PriceCents = 0;

	/** A unique override keeps purchases associated with this table if its actor or map is renamed. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Shop Table")
	FName OfferID;

	/** Optional name shown on the picker. Empty names use a numbered table label. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shop Table")
	FText DisplayName;

	static void GetOffers(const UWorld* World, TArray<FShopTableOffer>& OutOffers);

	/** Server entry point for the player interaction component after checking terminal access. */
	static EShopTablePurchaseResult Purchase(UWorld* World, FName OfferID, int64 ExpectedPriceCents);

private:
	FName GetPersistentOfferID() const;
	FVector GetMapLocation() const;
	static TMap<FName, const UShopTableOfferComponent*> CollectOffers(const UWorld& World);
};
