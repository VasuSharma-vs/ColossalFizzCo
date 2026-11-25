#include "Resources/HoseCableActor.h"
#include "Components/CollisionCableComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ManualColaFillingMachine.h"
#include "UObject/ConstructorHelpers.h"

namespace HoseInteraction
{
    void EnableConnectorQueries(UStaticMeshComponent* Mesh)
    {
        Mesh->SetCollisionObjectType(ECC_WorldDynamic);
        Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
        Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
}

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
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Spray(TEXT("/Game/Niagara/ColaOutlet/NS_ColaOutletJet.NS_ColaOutletJet"));
    RuptureSpray=Spray.Object;
    DischargeJet=CreateDefaultSubobject<UNiagaraComponent>(TEXT("DischargeJet"));
    DischargeJet->SetupAttachment(SceneRoot);
    DischargeJet->SetAutoActivate(false);
    StartEndpointMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StartEndpointMesh")); StartEndpointMesh->SetupAttachment(SceneRoot);
    EndEndpointMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndEndpointMesh")); EndEndpointMesh->SetupAttachment(SceneRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Connector(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    for (auto* Mesh:{StartEndpointMesh.Get(),EndEndpointMesh.Get()})
    {
        Mesh->SetStaticMesh(Connector.Object); HoseInteraction::EnableConnectorQueries(Mesh);
        Mesh->SetRelativeScale3D(FVector(.12,.12,.2)); Mesh->SetCanEverAffectNavigation(false);
    }
    StartMeshOffset.SetScale3D(FVector(.12,.12,.2)); EndMeshOffset.SetScale3D(FVector(.12,.12,.2));
    EndEndpointMesh->SetRelativeLocation(FVector(400,0,0));
}
void AHoseCableActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // Existing Blueprint presets serialized NoCollision on the inherited meshes.
    // Migrate those too; constructor defaults alone do not repair saved presets.
    HoseInteraction::EnableConnectorQueries(StartEndpointMesh);
    HoseInteraction::EnableConnectorQueries(EndEndpointMesh);
    UpdateConnectorMeshes();
}
void AHoseCableActor::BeginPlay()
{
    Super::BeginPlay(); Transfer->OnBurst.AddDynamic(this,&AHoseCableActor::HandleBurst);
    DischargeJet->SetAsset(RuptureSpray);
    HoseInteraction::EnableConnectorQueries(StartEndpointMesh);
    HoseInteraction::EnableConnectorQueries(EndEndpointMesh);
    if (HasAuthority())
    {
        RefreshCableAttachments();
        if (auto* Port=Cast<UResourceTransferEndpointComponent>(InitialStartPort.GetComponent(this))) ConnectStart(Port);
        if (auto* Port=Cast<UResourceTransferEndpointComponent>(InitialEndPort.GetComponent(this))) ConnectEnd(Port);
    }
    UpdateConnectorMeshes();
}
void AHoseCableActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
    {
        if (IsValid(StartPort)) StartPort->OnPhysicalHoseAttachmentChanged(this, false);
        if (IsValid(EndPort)) EndPort->OnPhysicalHoseAttachmentChanged(this, false);
    }
    Super::EndPlay(Reason);
}
void AHoseCableActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AHoseCableActor,StartPort); DOREPLIFETIME(AHoseCableActor,EndPort);
    DOREPLIFETIME(AHoseCableActor, bSourceAtEnd);
    DOREPLIFETIME(AHoseCableActor, StartHandGrip);
    DOREPLIFETIME(AHoseCableActor, EndHandGrip);
}
bool AHoseCableActor::CanAttach(UResourceTransferEndpointComponent* Port, bool bSupply, int32 ExpectedResourceID, bool bWaitForSource) const
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !IsValid(Port) || Port->GetWorld()!=GetWorld() || Port->GetOwner()==this
        || !IsValid(Port->GetOwner()) || !Port->GetOwner()->HasAuthority() || Transfer->bBurst || !Port->CanAcceptHose(Transfer)) return false;
    if (Port->bUseFixedHoseDirection && (bSupply ? !Port->bCanSupply : !Port->bCanReceive)) return false;
    if (bWaitForSource ? Port->Category != Transfer->Category : !Port->SupportsResource(Transfer->Category, ExpectedResourceID)) return false;
    const bool CanUseSocket=Transfer->Category==EResourceTransportCategory::Content ? Port->bCanSupply || Port->bCanReceive
        : bSupply ? Port->bCanSupply : Port->bCanReceive;
    if (!CanUseSocket) return false;
    const auto* Other=bSupply ? EndPort.Get() : StartPort.Get();
    return Other!=Port && (!Other || FVector::DistSquared(Port->GetComponentLocation(),Other->GetComponentLocation())
        <=FMath::Square(FMath::Max(1.f,Cable->CableLength)+FMath::Max(0.f,Cable->LengthSlackTolerance)));
}
bool AHoseCableActor::ConnectStart(UResourceTransferEndpointComponent* Port)
{
    return ConnectSupplyAtConnector(Port, GetSupplyConnectorIndex());
}
int32 AHoseCableActor::GetSupplyConnectorIndex() const
{
    return bSourceAtEnd ? Cable->NumSegments : 0;
}
int32 AHoseCableActor::GetReceivingConnectorIndex() const
{
    return bSourceAtEnd ? 0 : Cable->NumSegments;
}
bool AHoseCableActor::ConnectSupplyAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex)
{
    return ConnectPortAtConnector(Port, PointIndex, true, false);
}
bool AHoseCableActor::ConnectEnd(UResourceTransferEndpointComponent* Port)
{
    return ConnectPortAtConnector(Port, GetReceivingConnectorIndex(), false, false);
}
bool AHoseCableActor::ConnectReceivingAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex)
{
    return ConnectPortAtConnector(Port, PointIndex, false, true);
}

bool AHoseCableActor::IsUnassignedContentSource(const UResourceTransferEndpointComponent* Port) const
{
    return IsValid(Port) && Port->Category==EResourceTransportCategory::Content && Port->bCanSupply
        && Port->ResourceID==0 && Transfer->Category==EResourceTransportCategory::Content
        && bMatchSourceResourceWhenEmpty && Transfer->Buffer.Units<=0;
}

bool AHoseCableActor::ConnectPortAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex, bool bSupply,
    bool bAdoptReceiverResource, bool bWaitForSource)
{
    if (!IsValid(Port) || !GetConnectorMesh(PointIndex)) return false;
    const bool bPendingContent = bWaitForSource || IsUnassignedContentSource(bSupply ? Port : StartPort.Get());
    const bool bAdopt = !bPendingContent && bMatchSourceResourceWhenEmpty && Transfer->Buffer.Units<=0
        && (bSupply || (bAdoptReceiverResource && !StartPort));
    const int32 PreviousID = Transfer->ResourceID;
    const int32 ExpectedID = bAdopt ? Port->ResourceID : PreviousID;
    if (!CanAttach(Port, bSupply, ExpectedID, bPendingContent)) return false;

    const bool bNewSourceAtEnd = bSupply ? PointIndex!=0 : PointIndex==0;
    // Keep the other connector in place if the player has already hooked it up.
    if (bNewSourceAtEnd!=bSourceAtEnd && (IsValid(StartPort) || IsValid(EndPort))) return false;

    auto& Socket = bSupply ? StartPort : EndPort;
    const auto& Other = bSupply ? EndPort : StartPort;
    if (const auto* Inlet = Cast<UHoseSocketComponent>(Other.Get()); Inlet && !bPendingContent)
    {
        int32 PortIndex = INDEX_NONE;
        if (auto* Machine = Inlet->FindFillingMachine(PortIndex))
        {
            const int32 RequiredID = Machine->GetRequiredHoseContentID(PortIndex);
            if (RequiredID > 0 && RequiredID != ExpectedID) return false;
            // A parked inlet learns its type from this connection. A ready inlet can keep its existing binding.
            if ((!Inlet->IsManagedReceivingProxy() || !Inlet->SupportsResource(Transfer->Category, ExpectedID))
                && !Machine->PrepareReceivingHoseConnection(PortIndex, ExpectedID, this)) return false;
        }
    }
    Transfer->ResourceID = ExpectedID;
    if (Other && !bPendingContent && !Transfer->ConnectEndpoints(bSupply ? Port : Other.Get(), bSupply ? Other.Get() : Port))
    {
        Transfer->ResourceID = PreviousID;
        return false;
    }

    auto* PreviousPort = Socket.Get();
    Socket = Port;
    Transfer->FailureMode=EHoseFailureMode::None;
    bSourceAtEnd = bNewSourceAtEnd;
    (PointIndex==0 ? StartHandGrip : EndHandGrip) = FHoseHandGrip();
    RefreshCableAttachments(PointIndex);
    if (IsValid(PreviousPort) && PreviousPort!=Port) PreviousPort->OnPhysicalHoseAttachmentChanged(this, false);
    Port->OnPhysicalHoseAttachmentChanged(this, true);
    ForceNetUpdate();
    return true;
}

bool AHoseCableActor::RefreshSourceContent()
{
    if (!HasAuthority() || GetNetMode()==NM_Client || !IsValid(StartPort) || Transfer->bBurst
        || Transfer->Category!=EResourceTransportCategory::Content) return false;
    const int32 ContentID=StartPort->ResourceID;
    if (ContentID<=0) return false;
    if (Transfer->ResourceID==ContentID && (!EndPort || Transfer->HasValidConnection())) return true;

    int32 TablePort=INDEX_NONE;
    auto* Socket=Cast<UHoseSocketComponent>(EndPort.Get());
    auto* Table=Socket ? Socket->FindFillingMachine(TablePort) : nullptr;
    if (Table && Table->GetRequiredHoseContentID(TablePort)>0 && Table->GetRequiredHoseContentID(TablePort)!=ContentID) return false;
    if (EndPort && !Table && !EndPort->SupportsResource(EResourceTransportCategory::Content,ContentID)) return false;

    if (Transfer->ResourceID!=ContentID && Transfer->Buffer.Units>0.f)
    {
        Transfer->PurgeContents();
        if (Transfer->Buffer.Units>0.f) return false;
    }
    Transfer->DisconnectEndpoints();
    if (Table && (!Socket->IsManagedReceivingProxy() || !Socket->SupportsResource(EResourceTransportCategory::Content,ContentID))
        && !Table->PrepareReceivingHoseConnection(TablePort,ContentID,this)) return false;
    Transfer->ResourceID=ContentID;
    const bool bConnected=!EndPort || Transfer->ConnectEndpoints(StartPort,EndPort);
    ForceNetUpdate();
    return bConnected;
}

void AHoseCableActor::DisconnectStart() { DisconnectPort(StartPort); }
void AHoseCableActor::DisconnectEnd() { DisconnectPort(EndPort); }

void AHoseCableActor::EjectConnectorFrom(UResourceTransferEndpointComponent* Port,float Pressure)
{
    const bool bSourceSlot=StartPort==Port;
    if (!IsValid(Port) || (!bSourceSlot && EndPort!=Port)) return;
    const int32 Point=bSourceSlot ? GetSupplyConnectorIndex() : GetReceivingConnectorIndex();
    const FVector Outward=Port->GetUpVector();
    DisconnectPort(bSourceSlot ? StartPort : EndPort);
    if (Transfer->Category==EResourceTransportCategory::Content && FMath::IsFinite(Pressure))
        Cable->AddImpulseToPoint(Point,Outward*Cable->GetPointMass(Point)*FMath::Clamp(65.f+25.f*FMath::Abs(Pressure),65.f,260.f));
}
void AHoseCableActor::DisconnectPort(TObjectPtr<UResourceTransferEndpointComponent>& Port)
{
    if (!HasAuthority() || GetNetMode()==NM_Client) return;
    if (Port)
    {
        const int32 Connector=&Port==&StartPort ? GetSupplyConnectorIndex() : GetReceivingConnectorIndex();
        Transfer->CaptureUnpluggedPressure(Connector==0 ? EHoseDischargePoint::StartTip : EHoseDischargePoint::EndTip);
    }
    Transfer->DisconnectEndpoints();
    auto* PreviousPort = Port.Get();
    Port = nullptr;
    RefreshCableAttachments();
    if (IsValid(PreviousPort)) PreviousPort->OnPhysicalHoseAttachmentChanged(this, false);
    ForceNetUpdate();
}
void AHoseCableActor::RefreshCableAttachments(int32 ConnectorToSnap)
{
    auto UpdateGripTickSource = [this](TWeakObjectPtr<USceneComponent>& Previous, USceneComponent* Hand)
    {
        if (Previous.Get() == Hand) return;
        if (USceneComponent* OldHand = Previous.Get()) Cable->RemoveTickPrerequisiteComponent(OldHand);
        if (Hand) Cable->AddTickPrerequisiteComponent(Hand);
        Previous = Hand;
    };

    if (Cable->GetAttachParent()!=SceneRoot)
        Cable->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);

    const bool bStartHeld = IsValid(StartHandGrip.Component);
    StartEndpointMesh->SetCollisionResponseToChannel(ECC_Visibility,bStartHeld ? ECR_Ignore : ECR_Block);
    UpdateGripTickSource(StartGripTickSource, bStartHeld ? StartHandGrip.Component.Get() : nullptr);
    auto* PhysicalStartPort = bSourceAtEnd ? EndPort.Get() : StartPort.Get();
    USceneComponent* StartAnchor = bStartHeld ? StartHandGrip.Component.Get() : PhysicalStartPort;
    Cable->bAttachStart = IsValid(StartAnchor);
    Cable->SetStartAttachmentOverride(Cable->bAttachStart ? StartAnchor : nullptr, bStartHeld ? StartHandGrip.Socket : NAME_None);
    const bool bEndHeld = IsValid(EndHandGrip.Component);
    EndEndpointMesh->SetCollisionResponseToChannel(ECC_Visibility,bEndHeld ? ECR_Ignore : ECR_Block);
    UpdateGripTickSource(EndGripTickSource, bEndHeld ? EndHandGrip.Component.Get() : nullptr);
    auto* PhysicalEndPort = bSourceAtEnd ? StartPort.Get() : EndPort.Get();
    USceneComponent* EndAnchor = bEndHeld ? EndHandGrip.Component.Get() : PhysicalEndPort;
    Cable->bAttachEnd = IsValid(EndAnchor);
    if (Cable->bAttachEnd) Cable->EndLocation = FVector::ZeroVector;
    Cable->SetAttachEndToComponent(Cable->bAttachEnd ? EndAnchor : nullptr, bEndHeld ? EndHandGrip.Socket : NAME_None);
    Cable->SetSocketMountedEndpoints(!bStartHeld && IsValid(PhysicalStartPort), !bEndHeld && IsValid(PhysicalEndPort));
    Cable->WakeCable();
    if (ConnectorToSnap!=INDEX_NONE) Cable->SnapAttachedEndpoint(ConnectorToSnap==0);
    UpdateConnectorMeshes();
}
void AHoseCableActor::OnRep_Endpoints() { RefreshCableAttachments(); }
void AHoseCableActor::HandleBurst(float Pressure,float ReleasedUnits)
{
    if (Transfer->Category==EResourceTransportCategory::Content)
        UpdatePressureVisuals();
    else
    {
        StartHandGrip = FHoseHandGrip();
        EndHandGrip = FHoseHandGrip();
        DisconnectStart(); DisconnectEnd();
    }
    OnHoseRuptured(Pressure,ReleasedUnits);
}

void AHoseCableActor::UpdatePressureVisuals()
{
    if (Transfer->Category!=EResourceTransportCategory::Content)
    {
        Cable->SetHoseVisualState(1.f,false);
        return;
    }
    const float Pressure=Transfer->CurrentPressure;
    const float Rating=Pressure<0.f ? Transfer->VacuumCollapseThreshold : Transfer->BurstPressureThreshold;
    const float Severity=FMath::Clamp(FMath::Abs(Pressure)/FMath::Max(Rating,UE_KINDA_SMALL_NUMBER),0.f,1.f);
    const float Diameter=FMath::Lerp(1.f,Pressure<0.f ? .35f : 1.65f,Severity);
    Cable->SetHoseVisualState(Diameter,Transfer->bBurst);
}

bool AHoseCableActor::GetDischargeTransform(EHoseDischargePoint Point,FTransform& Transform) const
{
    if (Point==EHoseDischargePoint::StartTip || Point==EHoseDischargePoint::EndTip)
    {
        const auto* Mesh=Point==EHoseDischargePoint::StartTip ? StartEndpointMesh.Get() : EndEndpointMesh.Get();
        FVector Min,Max;
        Mesh->GetLocalBounds(Min,Max);
        FVector Mouth=(Min+Max)*.5;
        Mouth.Z=Max.Z;
        Transform=FTransform(Mesh->GetComponentQuat(),Mesh->GetComponentTransform().TransformPosition(Mouth));
        return !Transform.ContainsNaN();
    }
    if (Point!=EHoseDischargePoint::Rupture) return false;
    TArray<FVector> Points; Cable->GetCableRenderLocations(Points);
    if (Points.Num()<2) return false;
    const int32 Middle=(Points.Num()-2)/2;
    const FVector Tangent=(Points[Middle+1]-Points[Middle]).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
    const FVector RadialUp=FVector::VectorPlaneProject(FVector::UpVector,Tangent).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    const FVector Outward=Transfer->FailureMode==EHoseFailureMode::VacuumCollapse ? -RadialUp : RadialUp;
    const FVector Surface=(Points[Middle]+Points[Middle+1])*.5+Outward*(Cable->GetRenderingDiameter()*.5f);
    Transform=FTransform(FRotationMatrix::MakeFromZ(Outward+Tangent*.25f).ToQuat(),Surface);
    return !Transform.ContainsNaN();
}

void AHoseCableActor::UpdateDischargeJet()
{
    if (!FApp::CanEverRender() || GetNetMode()==NM_DedicatedServer || !DischargeJet->GetAsset()) return;
    const FHoseDischargeState& Discharge=Transfer->Discharge;
    FTransform Frame;
    if (Discharge.UnitsPerSecond<=0.f || !GetDischargeTransform(Discharge.Point,Frame))
    {
        if (bDischargeEmitting) DischargeJet->Deactivate();
        bDischargeEmitting=false;
        return;
    }
    DischargeJet->SetWorldLocationAndRotation(Frame.GetLocation(),Frame.GetRotation());
    DischargeJet->SetVariableFloat(TEXT("User.OutletFlow"),FMath::Clamp(Discharge.UnitsPerSecond,.25f,16.f));
    DischargeJet->SetVariableFloat(TEXT("User.OutletPressure"),FMath::Clamp(Discharge.Pressure,.35f,20.f));
    if (!bDischargeEmitting || DischargeColorContentID!=Transfer->ResourceID)
    {
        DischargeColorContentID=Transfer->ResourceID;
        FLinearColor Tint(.25f,.08f,.02f,1.f);
        if (const auto* Manager=APropManager::Get(this))
            if (const auto* Content=Manager->FindContentDefinition(DischargeColorContentID))
                Tint=Content->bOverrideSprayColor ? Content->SprayColor : Content->LiquidColor;
        DischargeJet->SetVariableLinearColor(TEXT("User.ColaSprayColor"),Tint);
    }
    if (!bDischargeEmitting) DischargeJet->Activate(true);
    bDischargeEmitting=true;
}

void AHoseCableActor::ApplyDischargeRecoil(float DeltaSeconds)
{
    const auto& Discharge=Transfer->Discharge;
    if (!HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f || !FMath::IsFinite(JetReactionStrength)
        || JetReactionStrength<=0.f || Discharge.UnitsPerSecond<=0.f || Discharge.Pressure<=0.f) return;

    const bool AtStart=Discharge.Point==EHoseDischargePoint::StartTip;
    if ((!AtStart && Discharge.Point!=EHoseDischargePoint::EndTip) || (AtStart ? Cable->bAttachStart : Cable->bAttachEnd)) return;
    const int32 Point=AtStart ? 0 : Cable->NumSegments;
    if (Cable->GetActiveGrabs().ContainsByPredicate([Point](const auto& Grab) { return Grab.PointIndex==Point; })) return;

    const FVector JetAxis=GetConnectorMesh(Point)->GetUpVector();
    const FVector Side=FVector::CrossProduct(FVector::UpVector,JetAxis).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    const FVector Lift=FVector::CrossProduct(JetAxis,Side);
    const float Step=FMath::Min(DeltaSeconds,.05f);
    JetReactionPhase=FMath::Fmod(JetReactionPhase+Step*12.f,2.f*PI);
    const FVector Direction=-JetAxis+Side*(.55f*FMath::Sin(JetReactionPhase))+Lift*(.68f*FMath::Cos(JetReactionPhase));
    const double JetThrust=260.*Discharge.UnitsPerSecond*FMath::Sqrt(Discharge.Pressure)*JetReactionStrength;
    const double Acceleration=FMath::Min(JetThrust,2300.);
    Cable->AddImpulseToPoint(Point,Direction*(Cable->GetPointMass(Point)*Acceleration*Step));
}
void AHoseCableActor::UpdateConnectorMeshes()
{
    TArray<FVector> Points;
    if (GetNetMode() == NM_Client && Cable->bEnableNetworkSync) Cable->GetCableRenderLocations(Points);
    else Cable->GetCableParticleLocations(Points);
    if (Points.Num()<2) return;
    auto UpdateConnector = [this](UStaticMeshComponent* Mesh, const FHoseHandGrip& Grip, const UResourceTransferEndpointComponent* Port, const FTransform& Offset,
        const FVector& Endpoint, const FVector& Neighbour)
    {
        if (IsValid(Grip.Component))
        {
            // Replicated grips attach the cylinder on observing clients too.
            if ((Mesh->GetAttachParent() != Grip.Component || Mesh->GetAttachSocketName() != Grip.Socket)
                && Mesh->AttachToComponent(Grip.Component, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Grip.Socket))
            {
                Mesh->SetRelativeLocationAndRotation(Offset.GetLocation(), Offset.GetRotation());
            }
            return;
        }

        const bool bUseSocketFrame = IsValid(Port) && Port->bUseAuthoredHoseConnectorFrame;
        if (bUseSocketFrame && Mesh->IsSimulatingPhysics()) Mesh->SetSimulatePhysics(false);
        if (Mesh->GetAttachParent() != SceneRoot) Mesh->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepWorldTransform);
        const FTransform EndFrame = bUseSocketFrame
            ? FTransform(Port->GetComponentQuat(),Port->GetComponentLocation())
            : FTransform(FRotationMatrix::MakeFromZ(Endpoint - Neighbour).ToQuat(),Endpoint);
        Mesh->SetWorldTransform(Offset * EndFrame);
    };
    UpdateConnector(StartEndpointMesh, StartHandGrip, bSourceAtEnd ? EndPort.Get() : StartPort.Get(), StartMeshOffset, Points[0], Points[1]);
    UpdateConnector(EndEndpointMesh, EndHandGrip, bSourceAtEnd ? StartPort.Get() : EndPort.Get(), EndMeshOffset, Points.Last(), Points[Points.Num()-2]);
}
void AHoseCableActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        auto ClearInvalidGrip = [this](FHoseHandGrip& Grip)
        {
            if (!Grip.Component || (Cable->bEnableGrabbing && IsValid(Grip.Component) && IsValid(Grip.Component->GetOwner()))) return false;
            Grip = FHoseHandGrip();
            return true;
        };
        const bool bReleasedStart = ClearInvalidGrip(StartHandGrip);
        const bool bReleasedEnd = ClearInvalidGrip(EndHandGrip);
        if (bReleasedStart || bReleasedEnd)
        {
            RefreshCableAttachments();
            ForceNetUpdate();
        }
        // Stop transport if an endpoint is destroyed, disabled or externally detached.
        const auto* PhysicalStartPort = bSourceAtEnd ? EndPort.Get() : StartPort.Get();
        const auto* PhysicalEndPort = bSourceAtEnd ? StartPort.Get() : EndPort.Get();
        if ((Cable->bAttachStart && !StartHandGrip.Component && (!IsValid(PhysicalStartPort) || Cable->GetStartAttachmentComponent()!=PhysicalStartPort))
            || (Cable->bAttachEnd && !EndHandGrip.Component && (!IsValid(PhysicalEndPort) || Cable->GetAttachedComponent()!=PhysicalEndPort)))
        { DisconnectStart(); DisconnectEnd(); }
    }
    UpdatePressureVisuals();
    UpdateConnectorMeshes();
    ApplyDischargeRecoil(DeltaSeconds);
    UpdateDischargeJet();
}
