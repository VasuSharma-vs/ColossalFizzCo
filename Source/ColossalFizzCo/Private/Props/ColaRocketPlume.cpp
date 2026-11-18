#include "Props/ColaPropActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Props/ColaRocketPlumeSubsystem.h"
#include "Props/InteractablePropManager.h"

bool AColaPropActor::TryGetRocketSprayColor(FLinearColor& OutColor) const
{
	const auto* Content = ResolveContentDefinition();
	if (!Content)
	{
		OutColor = FLinearColor(.25f, .08f, .02f, 1.f);
		return false;
	}

	OutColor = Content->bOverrideSprayColor ? Content->SprayColor : Content->LiquidColor;
	if (!Content->bOverrideSprayColor)
	{
		static const FHashedMaterialParameterInfo Tint(TEXT("Liquid Color"));
		FLinearColor MaterialColor;
		if (Content->LiquidMaterial && Content->LiquidMaterial->GetVectorParameterValue(Tint, MaterialColor))
		{
			OutColor = MaterialColor;
		}
		else if (const auto* Visual = PropManagerReference->FindBottleContentVisual(GetContentID(), ProductID))
		{
			// Some definitions specify appearance only on a bottle-specific liquid mesh.
			const UMaterialInterface* Material = Visual->Materials.IsEmpty() ? nullptr : Visual->Materials[0].Get();
			if (Material && Material->GetVectorParameterValue(Tint, MaterialColor))
			{
				OutColor = MaterialColor;
			}
		}
	}
	OutColor.A = 1.f;
	return true;
}

void AColaPropActor::UpdateRocketPlume()
{
	// Both flight modes and receiving clients already resolve this state locally.
	const bool bPowered = RocketComp && RocketComp->IsPoweredFlight();
	if (bRocketNozzleRegistered == bPowered)
	{
		return;
	}
	if (!bPowered)
	{
		StopRocketPlume();
		return;
	}

	if (!PropMesh || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender())
	{
		return;
	}
	auto* Plumes = GetWorld()->GetSubsystem<UColaRocketPlumeSubsystem>();
	if (!Plumes)
	{
		return;
	}

	USceneComponent* Parent = PropMesh;
	FName Socket = NAME_None;
	FTransform Nozzle;
	if (SkeletalMeshComponent && SkeletalMeshComponent->DoesSocketExist(RocketNozzleSocket))
	{
		Parent = SkeletalMeshComponent;
		Socket = RocketNozzleSocket;
	}
	else if (PropMesh->DoesSocketExist(RocketNozzleSocket))
	{
		Socket = RocketNozzleSocket;
	}
	else
	{
		if (const FPropProductDefinition* Definition = ResolveProductDefinition())
		{
			Nozzle = Definition->CapAttachmentTransform;
		}
		// Modular caps share the shell pivot. Their zero mount translation is
		// an assembly alignment, while the visible opening is at the shell tip.
		if (Nozzle.GetTranslation().IsNearlyZero() && PropMesh->GetStaticMesh())
		{
			const FBox Bounds = PropMesh->GetStaticMesh()->GetBoundingBox();
			const FVector Axis = Nozzle.GetRotation().GetAxisZ();
			const double TipDistance = FVector::DotProduct(Axis.GetAbs(), Bounds.GetExtent());
			Nozzle.SetTranslation(Bounds.GetCenter() + Axis * TipDistance);
		}
	}

	Plumes->SetBottleNozzle(this, Parent, Socket, Nozzle);
	bRocketNozzleRegistered = true;
}

void AColaPropActor::StopRocketPlume()
{
	if (!bRocketNozzleRegistered) return;
	if (UWorld* World = GetWorld())
		if (auto* Plumes = World->GetSubsystem<UColaRocketPlumeSubsystem>())
			Plumes->RemoveBottle(this);
	bRocketNozzleRegistered = false;
}

void AColaPropActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRocketPlume();
	Super::EndPlay(EndPlayReason);
}
