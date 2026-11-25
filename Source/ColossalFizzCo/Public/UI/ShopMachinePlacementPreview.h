#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopMachinePlacementPreview.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/** Local, visual-only copy of shop equipment used during placement. */
UCLASS(NotBlueprintable)
class COLOSSALFIZZCO_API AShopMachinePlacementPreview : public AActor
{
	GENERATED_BODY()

public:
	AShopMachinePlacementPreview();
	bool InitializeFromMachine(const AActor& Machine);
	void SetPlacementPreview(const FTransform& Pose, bool bCanPlace);

private:
	void AddPreviewMesh(const UStaticMeshComponent& Source, const FTransform& MachinePose);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterialAsset;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;
	TOptional<bool> PreviousCanPlace;
};
