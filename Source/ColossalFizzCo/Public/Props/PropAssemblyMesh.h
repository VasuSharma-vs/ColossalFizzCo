#pragma once

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PropAssemblyMesh.generated.h"

/** Local presentation component. The manager owns the persistent instance and its replication. */
UCLASS()
class COLOSSALFIZZCO_API UPropAssemblyMesh : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	/** The inner mesh may be shorter than its shell and have a nonzero base pivot. */
	static void ConfigureLiquidBounds(UStaticMesh* Mesh, UMaterialInstanceDynamic* Material)
	{
		if (!Mesh || !Material) return;
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Size = Bounds.GetSize();
		if (Size.Z <= UE_SMALL_NUMBER) return;
		Material->SetScalarParameterValue(TEXT("Liquid Container Height"), Size.Z);
		Material->SetScalarParameterValue(TEXT("Liquid Container Radius"), .5 * FMath::Max(Size.X, Size.Y));
		Material->SetScalarParameterValue(TEXT("Pivot at base"), 2.0 * Bounds.GetCenter().Z / Size.Z);
	}
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prop Identity") int32 ISID = 0;
	int32 ProductID = 0;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> DeformableVisual;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override
	{
		if (DeformableVisual) DeformableVisual->DestroyComponent();
		Super::OnComponentDestroyed(bDestroyingHierarchy);
	}
};
