#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopSupplyDepot.generated.h"

class UShopProductionStationComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Separate purchasing point. Filling machines only accept physical supplies. */
UCLASS()
class COLOSSALFIZZCO_API AShopSupplyDepot : public AActor
{
	GENERATED_BODY()
public:
	AShopSupplyDepot();
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Counter;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UShopProductionStationComponent> Supplies;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USceneComponent> DeliveryPoint;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Sign;
};
