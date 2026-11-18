#include "Resources/ResourceReservoirActor.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/ResourcePumpComponent.h"
#include "Components/CablePullResponseComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AResourceReservoirActor::AResourceReservoirActor()
{
    bReplicates=true; SetReplicateMovement(true);
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body")); SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Body->SetStaticMesh(Mesh.Object); Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    ResourcePort=CreateDefaultSubobject<UResourceTransferEndpointComponent>(TEXT("ResourcePort"));
    ResourcePort->SetupAttachment(Body); ResourcePort->SetRelativeLocation(FVector(0,0,60));
    PumpMotor=CreateDefaultSubobject<UResourcePumpComponent>(TEXT("PumpMotor")); ResourcePort->ConnectedMotor=PumpMotor;
    PullResponse=CreateDefaultSubobject<UCablePullResponseComponent>(TEXT("PullResponse"));
}
UResourceTransferEndpointComponent* AResourceReservoirActor::GetResourceTransferEndpoint_Implementation() const { return ResourcePort; }
