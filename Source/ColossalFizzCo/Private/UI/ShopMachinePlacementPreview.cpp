#include "UI/ShopMachinePlacementPreview.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/SocketColaFillingMachine.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float PreviewOpacity = 0.5f;
	const FLinearColor ClearTint(0.20f, 1.0f, 0.58f, PreviewOpacity);
	const FLinearColor BlockedTint(1.0f, 0.17f, 0.18f, PreviewOpacity);
}

AShopMachinePlacementPreview::AShopMachinePlacementPreview()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT(
		"/Game/Assest/Blueprints/FillingMachine/M_FillingMachinePlacementPreview.M_FillingMachinePlacementPreview"));
	PreviewMaterialAsset = Material.Get();
}

bool AShopMachinePlacementPreview::InitializeFromMachine(const AActor& Machine)
{
	if (PreviewMaterial || Machine.GetWorld() != GetWorld()) return false;
	auto* Material = PreviewMaterialAsset.Get();
	if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, TEXT(
		"/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
	if (!Material) return false;
	PreviewMaterial = UMaterialInstanceDynamic::Create(Material, this);
	if (!PreviewMaterial) return false;
	PreviewMaterial->SetScalarParameterValue(TEXT("Opacity"), PreviewOpacity);

	const FTransform MachinePose = Machine.GetActorTransform();
	const auto AddVisibleMeshes = [&](const AActor& SourceActor)
	{
		int32 Added = 0;
		TInlineComponentArray<UStaticMeshComponent*> Components;
		SourceActor.GetComponents(Components);
		for (const auto* Mesh : Components)
		{
			if (!Mesh->GetStaticMesh() || !Mesh->GetVisibleFlag() || Mesh->bHiddenInGame) continue;
			AddPreviewMesh(*Mesh, MachinePose);
			++Added;
		}
		return Added;
	};
	int32 Added = AddVisibleMeshes(Machine);
	TArray<AActor*> Attached;
	Machine.GetAttachedActors(Attached, true, true);
	AActivePropActor* SeatedBottle = nullptr;
	if (const auto* Table = Cast<ASocketColaFillingMachine>(&Machine); Table && Table->PropManagerReference && Table->Production)
	{
		auto* Bottle = Table->PropManagerReference->GetActiveActorFromISID(Table->Production->View.SeatedBottleISID);
		if (IsValid(Bottle) && Bottle->IsSeatedInFiller())
		{
			SeatedBottle = Bottle;
			Attached.AddUnique(Bottle);
		}
	}
	for (const AActor* Actor : Attached)
	{
		const auto* Prop = Cast<AActivePropActor>(Actor);
		if (!IsValid(Prop) || !Prop->bIsActive) continue;
		const int32 PropsAdded = AddVisibleMeshes(*Prop);
		Added += PropsAdded;
		// The active bottle can render a skeletal shell instead of its matching static mesh.
		if (!PropsAdded && Prop == SeatedBottle && Prop->PropMesh && Prop->PropMesh->GetStaticMesh())
		{
			AddPreviewMesh(*Prop->PropMesh, MachinePose);
			++Added;
		}
	}
	return Added > 0;
}

void AShopMachinePlacementPreview::AddPreviewMesh(const UStaticMeshComponent& Source, const FTransform& MachinePose)
{
	auto* Mesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	AddInstanceComponent(Mesh);
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Source.GetStaticMesh());
	Mesh->SetRelativeTransform(Source.GetComponentTransform().GetRelativeTransform(MachinePose));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetAffectDistanceFieldLighting(false);
	Mesh->SetVisibleInRayTracing(false);
	Mesh->SetReceivesDecals(false);
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Mesh->SetMaterial(Slot, PreviewMaterial.Get());
	Mesh->RegisterComponent();
}

void AShopMachinePlacementPreview::SetPlacementPreview(const FTransform& Pose, bool bCanPlace)
{
	if (!PreviewMaterial || !Pose.IsValid()) return;
	if (!PreviousCanPlace.IsSet() || PreviousCanPlace.GetValue() != bCanPlace)
	{
		const FLinearColor Tint = bCanPlace ? ClearTint : BlockedTint;
		PreviewMaterial->SetVectorParameterValue(TEXT("Tint"), Tint);
		PreviewMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
		PreviousCanPlace = bCanPlace;
	}
	SetActorTransform(Pose, false, nullptr, ETeleportType::TeleportPhysics);
	if (IsHidden()) SetActorHiddenInGame(false);
}
