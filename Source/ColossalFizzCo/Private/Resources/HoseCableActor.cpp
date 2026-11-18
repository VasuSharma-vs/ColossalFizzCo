#include "Resources/HoseCableActor.h"
#include "Components/CollisionCableComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AHoseCableActor::AHoseCableActor()
{
    bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("HoseRoot")); SetRootComponent(SceneRoot);
    Cable=CreateDefaultSubobject<UCollisionCableComponent>(TEXT("Cable")); Cable->SetupAttachment(SceneRoot);
    Cable->CableLength=400; Cable->EndLocation=FVector(400,0,0); Cable->NumSegments=24; Cable->CableWidth=4;
    Cable->PointMassKg=.05f; Cable->bAttachStart=false; Cable->bAttachEnd=false;
    Cable->bStartAttachmentIsWorldAnchor=false; Cable->bEndAttachmentIsWorldAnchor=false;
    Transfer=CreateDefaultSubobject<UHoseTransferComponent>(TEXT("Transfer"));
    StartEndpointMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StartEndpointMesh")); StartEndpointMesh->SetupAttachment(SceneRoot);
    EndEndpointMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndEndpointMesh")); EndEndpointMesh->SetupAttachment(SceneRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Connector(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    for (auto* Mesh:{StartEndpointMesh.Get(),EndEndpointMesh.Get()})
    {
        Mesh->SetStaticMesh(Connector.Object); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetRelativeScale3D(FVector(.12,.12,.2)); Mesh->SetCanEverAffectNavigation(false);
    }
    StartMeshOffset.SetScale3D(FVector(.12,.12,.2)); EndMeshOffset.SetScale3D(FVector(.12,.12,.2));
    EndEndpointMesh->SetRelativeLocation(FVector(400,0,0));
}
void AHoseCableActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform); UpdateConnectorMeshes();
}
void AHoseCableActor::BeginPlay()
{
    Super::BeginPlay(); Transfer->OnBurst.AddDynamic(this,&AHoseCableActor::HandleBurst);
    if (HasAuthority())
    {
        if (auto* Port=Cast<UResourceTransferEndpointComponent>(InitialStartPort.GetComponent(this))) ConnectStart(Port);
        if (auto* Port=Cast<UResourceTransferEndpointComponent>(InitialEndPort.GetComponent(this))) ConnectEnd(Port);
    }
    UpdateConnectorMeshes();
}
void AHoseCableActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AHoseCableActor,StartPort); DOREPLIFETIME(AHoseCableActor,EndPort);
}
bool AHoseCableActor::CanAttach(UResourceTransferEndpointComponent* Port,bool bStart) const
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !IsValid(Port) || Port->GetWorld()!=GetWorld() || Port->GetOwner()==this
        || !Port->GetOwner()->HasAuthority() || Transfer->bBurst || !Port->CanAcceptHose(Transfer) || !Port->SupportsResource(Transfer->Category,
            bStart && bMatchSourceResourceWhenEmpty && Transfer->Buffer.Units<=0 ? Port->ResourceID : Transfer->ResourceID)
        || (bStart ? !Port->bCanSupply : !Port->bCanReceive)) return false;
    const auto* Other=bStart ? EndPort.Get() : StartPort.Get();
    return Other!=Port && (!Other || FVector::DistSquared(Port->GetComponentLocation(),Other->GetComponentLocation())
        <=FMath::Square(FMath::Max(1.f,Cable->CableLength)+FMath::Max(0.f,Cable->LengthSlackTolerance)));
}
bool AHoseCableActor::ConnectStart(UResourceTransferEndpointComponent* Port)
{
    if (!CanAttach(Port,true)) return false;
    const int32 PreviousID=Transfer->ResourceID;
    if (bMatchSourceResourceWhenEmpty && Transfer->Buffer.Units<=0) Transfer->ResourceID=Port->ResourceID;
    if (EndPort && !Transfer->ConnectEndpoints(Port,EndPort)) { Transfer->ResourceID=PreviousID; return false; }
    StartPort=Port; RefreshCableAttachments(); Cable->ResetCable(); ForceNetUpdate(); return true;
}
bool AHoseCableActor::ConnectEnd(UResourceTransferEndpointComponent* Port)
{
    if (!CanAttach(Port,false)) return false;
    if (StartPort && !Transfer->ConnectEndpoints(StartPort,Port)) return false;
    EndPort=Port; RefreshCableAttachments(); Cable->ResetCable(); ForceNetUpdate(); return true;
}
void AHoseCableActor::DisconnectStart()
{
    if (!HasAuthority() || GetNetMode()==NM_Client) return;
    Transfer->DisconnectEndpoints(); StartPort=nullptr; RefreshCableAttachments(); ForceNetUpdate();
}
void AHoseCableActor::DisconnectEnd()
{
    if (!HasAuthority() || GetNetMode()==NM_Client) return;
    Transfer->DisconnectEndpoints(); EndPort=nullptr; RefreshCableAttachments(); ForceNetUpdate();
}
void AHoseCableActor::RefreshCableAttachments()
{
    if (IsValid(StartPort))
    {
        Cable->AttachToComponent(StartPort,FAttachmentTransformRules::SnapToTargetNotIncludingScale); Cable->bAttachStart=true;
    }
    else
    {
        Cable->AttachToComponent(SceneRoot,FAttachmentTransformRules::KeepWorldTransform); Cable->bAttachStart=false;
    }
    if (IsValid(EndPort)) { Cable->EndLocation=FVector::ZeroVector; Cable->bAttachEnd=true; Cable->SetAttachEndToComponent(EndPort); }
    else { Cable->bAttachEnd=false; Cable->SetAttachEndToComponent(nullptr); }
    Cable->WakeCable(); UpdateConnectorMeshes();
}
void AHoseCableActor::OnRep_Endpoints() { RefreshCableAttachments(); }
void AHoseCableActor::HandleBurst(float Pressure,float ReleasedUnits)
{
    DisconnectStart(); DisconnectEnd(); OnHoseRuptured(Pressure,ReleasedUnits);
}
void AHoseCableActor::UpdateConnectorMeshes()
{
    TArray<FVector> Points; Cable->GetCableParticleLocations(Points);
    if (Points.Num()<2) return;
    const FTransform StartFrame(FRotationMatrix::MakeFromZ(Points[0]-Points[1]).ToQuat(),Points[0]);
    const FTransform EndFrame(FRotationMatrix::MakeFromZ(Points.Last()-Points[Points.Num()-2]).ToQuat(),Points.Last());
    StartEndpointMesh->SetWorldTransform(StartMeshOffset*StartFrame); EndEndpointMesh->SetWorldTransform(EndMeshOffset*EndFrame);
}
void AHoseCableActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        // Stop transport if an endpoint is destroyed, disabled or externally detached.
        if ((Cable->bAttachStart && (!IsValid(StartPort) || Cable->GetAttachParent()!=StartPort))
            || (Cable->bAttachEnd && (!IsValid(EndPort) || Cable->GetAttachedComponent()!=EndPort)))
        { DisconnectStart(); DisconnectEnd(); }
    }
    UpdateConnectorMeshes();
}
