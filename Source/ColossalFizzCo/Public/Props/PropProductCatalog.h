#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/PropProductReference.h"
#include "PropProductCatalog.generated.h"

/** Bottle capacity and physical settings; liquid identity and mutable amount are not bottle-type identity. */
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UBottleProductDataAsset : public UInteractablePropDataAsset
{
 GENERATED_BODY()
public:
 UBottleProductDataAsset();
 UFUNCTION(BlueprintCallable,Category="Products|Migration") void ImportLegacyBottle(const UInteractablePropDataAsset* Source);
};

/** Content has no capacity/current-portion property. Its geometry is selected using the owner's bottle PID. */
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropContentDataAsset : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 UPropContentDataAsset();
 UFUNCTION(BlueprintCallable,Category="Products|Migration") void ImportLegacyContent(const UInteractablePropDataAsset* Source);
 UPROPERTY(EditAnywhere,Category="Content") FPropContentDefinition Properties;
 UPROPERTY(EditAnywhere,Category="Supported Bottles") TArray<FPropBottleContentVisual> Bottles;
};

UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropCapDataAsset : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,Category="Cap") TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,Category="Cap") TArray<TObjectPtr<UMaterialInterface>> Materials;
 UPROPERTY(EditAnywhere,Category="Cap") TSubclassOf<AActivePropActor> ActiveActorClass;
 UPROPERTY(EditAnywhere,Category="Cap",meta=(ClampMin="0.001",Units="kg")) float MassKg=.2f;
 UPROPERTY(EditAnywhere,Category="Cap") EBottleCapType Type=EBottleCapType::Twist;
 UPROPERTY(EditAnywhere,Category="Cap") FTransform MountOffset;
 UPROPERTY(EditAnywhere,Category="Cap") FPropPhysicsProfile Physics;
 UPROPERTY(EditAnywhere,Category="Cap",meta=(ClampMin="0")) float ReleasePressure=1.5f;
 UPROPERTY(EditAnywhere,Category="Cap",meta=(ClampMin="0")) float SafeOpenPressure=1.25f;
 UPROPERTY(EditAnywhere,Category="Cap",meta=(ClampMin="0",ClampMax="1")) float TorqueMinimum=.41f;
 UPROPERTY(EditAnywhere,Category="Cap",meta=(ClampMin="0",ClampMax="1")) float TorqueMaximum=.75f;
 UPROPERTY(EditAnywhere,Category="Supported Bottles",meta=(ProductCategory="Bottle")) TArray<FPropProductReference> SupportedBottles;
};

UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropBottleDatabase : public UDataAsset
{ GENERATED_BODY()
public: UPROPERTY(EditAnywhere,Category="Bottles") TArray<TSoftObjectPtr<UBottleProductDataAsset>> Products;
};
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropContentDatabase : public UDataAsset
{ GENERATED_BODY()
public: UPROPERTY(EditAnywhere,Category="Contents") TArray<TSoftObjectPtr<UPropContentDataAsset>> Products;
};
UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropCapDatabase : public UDataAsset
{ GENERATED_BODY()
public: UPROPERTY(EditAnywhere,Category="Caps") TArray<TSoftObjectPtr<UPropCapDataAsset>> Products;
};

/** Identity registry entry, never an instance record or a second copy of gameplay settings. */
USTRUCT(BlueprintType)
struct COLOSSALFIZZCO_API FPropCatalogEntry
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 PID=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FText Name;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) EPropProductCategory Category=EPropProductCategory::Bottle;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UPrimaryDataAsset> Definition;
 /** Explicit compatibility alias for an already-persisted PID. New products use their own primary entry. */
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 CanonicalPID=0;
 UPROPERTY() FGuid Identity;
};

UCLASS(BlueprintType)
class COLOSSALFIZZCO_API UPropProductCatalog : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,Category="Databases") TObjectPtr<UPropBottleDatabase> Bottles;
 UPROPERTY(EditAnywhere,Category="Databases") TObjectPtr<UPropContentDatabase> Contents;
 UPROPERTY(EditAnywhere,Category="Databases") TObjectPtr<UPropCapDatabase> Caps;
 UPROPERTY(EditAnywhere,Category="Identity Registry") TArray<FPropCatalogEntry> Products;
 /** Asset-path identity redirects for old combined authoring assets, never instance relationships. */
 UPROPERTY(EditAnywhere,Category="Migration") TMap<TSoftObjectPtr<UPrimaryDataAsset>,FPropProductReference> LegacyAssets;
 UFUNCTION(BlueprintPure,Category="Products") FText GetProductName(FPropProductReference Product) const;
 UFUNCTION(BlueprintPure,Category="Products") int32 ResolveCanonicalPID(int32 PID) const;
 UFUNCTION(CallInEditor,Category="Identity Registry") void RegisterDatabaseProducts();
 const FPropCatalogEntry* Find(int32 PID) const;
 const FPropCatalogEntry* FindByAsset(const UObject* Asset) const;
 UFUNCTION(BlueprintCallable,Category="Identity Registry") int32 RegisterIdentity(UPrimaryDataAsset* Asset,EPropProductCategory Category,const FText& Name,int32 ExistingPID=0,int32 CanonicalPID=0);
 UFUNCTION(BlueprintPure,Category="Identity Registry") TArray<FString> ValidateCatalog() const;
 UFUNCTION(BlueprintCallable,Category="Identity Registry") void ReserveLegacyPIDs(const TArray<int32>& PIDs);
 void AssignMissingPIDs();
#if WITH_EDITOR
 virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
 virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
private:
 UPROPERTY() int64 NextPID=1000;
 UPROPERTY() TSet<int32> IssuedPIDs;
};
