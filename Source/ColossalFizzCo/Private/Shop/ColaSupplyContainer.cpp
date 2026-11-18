#include "Shop/ColaSupplyContainer.h"
#include "Components/CollisionCableComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AColaSupplyContainer::AColaSupplyContainer()
{
	bReplicates=true; SetReplicateMovement(true); bAutoConvertToISMIfAtRest=false;
	SupplyHose=CreateDefaultSubobject<UCollisionCableComponent>(TEXT("SupplyHose"));
	SupplyHose->SetupAttachment(PropMesh); SupplyHose->SetRelativeLocation(FVector(0,0,65));
	SupplyHose->CableLength=400; SupplyHose->NumSegments=16; SupplyHose->CableWidth=4;
	SupplyHose->CollisionPointsPerFrame=4; SupplyHose->bAttachEnd=false; SupplyHose->EndLocation=FVector(180,0,0);
	SupplyHose->bEnableGrabbing=true;
	SupplyHose->bStartAttachmentIsWorldAnchor=false;
	SupplyPort=CreateDefaultSubobject<UResourceTransferEndpointComponent>(TEXT("SupplyPort"));
	SupplyPort->SetupAttachment(PropMesh); SupplyPort->SetRelativeLocation(FVector(0,0,65)); SupplyPort->bCanReceive=false;
	SupplyTransfer=CreateDefaultSubobject<UHoseTransferComponent>(TEXT("SupplyTransfer"));
	SupplyTransfer->PrimaryComponentTick.bStartWithTickEnabled=false;
	SupplyTransfer->MaxUnitsPerSecond=10.f;
	CablePullResponse=CreateDefaultSubobject<UCablePullResponseComponent>(TEXT("CablePullResponse"));
	CablePullResponse->WeightKg=20; CablePullResponse->ResistanceNewtons=40;
	SupplyNozzle=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SupplyNozzle"));
	SupplyNozzle->SetupAttachment(PropMesh); SupplyNozzle->SetRelativeLocation(FVector(180,0,65));
	SupplyNozzle->SetRelativeScale3D(FVector(.12,.12,.25));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SupplyNozzle->SetStaticMesh(Cylinder.Object); SupplyNozzle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SupplyConnector=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SupplyConnector")); SupplyConnector->SetupAttachment(SupplyHose);
	SupplyConnector->SetStaticMesh(Cylinder.Object); SupplyConnector->SetRelativeScale3D(FVector(.16,.16,.12));
	SupplyConnector->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SupplyNozzle->SetIsReplicated(true); SupplyNozzle->ComponentTags.Add(TEXT("ManualFillingSupplyNozzle"));
	OnPropBecomeActive.AddDynamic(this,&AColaSupplyContainer::PrepareDeliveredHose);
}
void AColaSupplyContainer::PrepareDeliveredHose(AActivePropActor* Prop,int32 InstanceID,int32 DefinitionID,FTransform Transform)
{
	if (PropManagerReference && LocalUniqueData.EventData.ContentID>0) SupplyPort->ConfigureManagedContent(PropManagerReference,InstanceID,LocalUniqueData.EventData.ContentID);
	// Pool actors start away from the level. Seed the delivered hose only after
	// the managed prop has reached its final gameplay transform.
	SupplyNozzle->AttachToComponent(PropMesh,FAttachmentTransformRules::KeepRelativeTransform);
	SupplyNozzle->SetRelativeLocationAndRotation(FVector(180,0,65),FRotator::ZeroRotator);
	SupplyHose->SetAttachEndToComponent(nullptr); SupplyHose->bAttachEnd=false;
	SupplyHose->EndLocation=FVector(180,0,0); SupplyHose->ResetCable();
}
UResourceTransferEndpointComponent* AColaSupplyContainer::GetResourceTransferEndpoint_Implementation() const { return SupplyPort; }
bool AColaSupplyContainer::MountHose(USceneComponent* Port)
{
	if (!HasAuthority() || !IsValid(Port) || !SupplyNozzle->AttachToComponent(Port,FAttachmentTransformRules::SnapToTargetNotIncludingScale)) return false;
	SupplyHose->EndLocation=FVector::ZeroVector; SupplyHose->bAttachEnd=true; SupplyHose->SetAttachEndToComponent(Port); SupplyHose->WakeCable(); ForceNetUpdate(); return true;
}
void AColaSupplyContainer::ReleaseHose()
{
	if (!HasAuthority()) return;
	SupplyTransfer->DisconnectEndpoints();
	TArray<FVector> Positions; SupplyHose->GetCableParticleLocations(Positions);
	SupplyNozzle->AttachToComponent(PropMesh,FAttachmentTransformRules::KeepWorldTransform);
	SupplyHose->bAttachEnd=false; SupplyHose->SetAttachEndToComponent(nullptr);
	if (!Positions.IsEmpty()) SupplyHose->EndLocation=SupplyHose->GetComponentTransform().InverseTransformPosition(Positions.Last());
	if (Positions.Num()==SupplyHose->NumSegments+1) SupplyHose->SetCableParticleLocations(Positions);
	SupplyHose->WakeCable(); ForceNetUpdate();
}
void AColaSupplyContainer::ResetTransientPoolState()
{
	ReleaseHose(); Super::ResetTransientPoolState();
}
void AColaSupplyContainer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsActive || !SupplyNozzle || !SupplyHose || SupplyHose->bAttachEnd) return;
	TArray<FVector> Positions; SupplyHose->GetCableParticleLocations(Positions);
	if (Positions.Num()>1)
		SupplyNozzle->SetWorldLocationAndRotation(Positions.Last(),FRotationMatrix::MakeFromX(Positions.Last()-Positions[Positions.Num()-2]).ToQuat());
}
