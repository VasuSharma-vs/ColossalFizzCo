#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Props/InteractablePropDatabase.h"
#include "ShopSupplyCatalog.generated.h"

class UInteractablePropDataAsset;

UENUM(BlueprintType)
enum class EShopSupplyKind : uint8 { EmptyBottle, Cap, ColaCylinder, ManualFillingMachine, TemporarySeal };

UENUM(BlueprintType)
enum class ENormalColaFlavor : uint8 { Lemonade, Mint, Sweet, Spicy, ZeroCalorie };

USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FShopSupplyOffer
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OfferID;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EShopSupplyKind Kind = EShopSupplyKind::EmptyBottle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ENormalColaFlavor Flavor = ENormalColaFlavor::Lemonade;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UInteractablePropDataAsset> Product;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int64 UnitCostCents = 100;
};

USTRUCT(BlueprintType)
struct FShopBottleRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> Shell;
	UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> ActiveShell;
	UPROPERTY(EditAnywhere) FTransform LiquidMount;
	UPROPERTY(EditAnywhere) FTransform CapMount;
	UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UInteractablePropDataAsset>> CompatibleCaps;
};

USTRUCT(BlueprintType)
struct FShopLiquidVisual
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> ActiveMesh;
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UMaterialInterface> Material;
};

USTRUCT(BlueprintType)
struct FShopContentRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TArray<EBottleMaterialType> SupportedBottles;
	UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UInteractablePropDataAsset>> SupportedCaps;
	UPROPERTY(EditAnywhere) TMap<EBottleMaterialType,FShopLiquidVisual> VisualByBottle;
};

USTRUCT(BlueprintType)
struct FShopCapRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere) TArray<EBottleMaterialType> SupportedBottles;
	UPROPERTY(EditAnywhere, meta=(Units="kg",ClampMin="0")) float MassKg = .2f;
	UPROPERTY(EditAnywhere) FTransform MountOffset;
};

/** Authored procurement offers. Prices and deliverable products never come from player requests. */
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UShopSupplyCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UShopSupplyCatalog();
	virtual void PostLoad() override;
	UPROPERTY(EditAnywhere, Category="Assembly|Bottles") TMap<EBottleMaterialType,FShopBottleRow> Bottles;
	UPROPERTY(EditAnywhere, Category="Assembly|Contents") TMap<int32,FShopContentRow> Contents;
	UPROPERTY(EditAnywhere, Category="Assembly|Caps") TMap<TSoftObjectPtr<UInteractablePropDataAsset>,FShopCapRow> Caps;
	UPROPERTY(EditAnywhere, Category="Assembly|Bottles") TSoftObjectPtr<UStaticMesh> EmptyLiquidMesh;
	UPROPERTY(EditAnywhere, Category="Assembly|Bottles") TSoftObjectPtr<USkeletalMesh> DefaultActiveLiquidMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FShopSupplyOffer> Offers;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UInteractablePropDataAsset> EmptyBottleProduct;
	/** Exactly the five authored Normal Cola products, in ENormalColaFlavor order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UInteractablePropDataAsset>> FinishedBottleProducts;
	const FShopSupplyOffer* FindOffer(FName OfferID) const;
};
