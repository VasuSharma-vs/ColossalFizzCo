#include "Resources/PressurizedColaContainer.h"
#include "Resources/HoseCableActor.h"
#include "Components/BoxComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Props/InteractablePropManager.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "UObject/ConstructorHelpers.h"

APressurizedColaContainer::APressurizedColaContainer()
{
    bReplicates=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=true;
    ContainerRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ContainerRoot")); SetRootComponent(ContainerRoot);
    auto MakeControlVolume = [this](FName Name, USceneComponent* Parent, const FVector& Extent)
    {
        auto* Volume = CreateDefaultSubobject<UBoxComponent>(Name);
        Volume->SetupAttachment(Parent);
        Volume->SetBoxExtent(Extent);
        Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
        Volume->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Volume->SetGenerateOverlapEvents(false);
        Volume->SetHiddenInGame(true);
        Volume->bEditableWhenInherited = true;
        Volume->ComponentTags.Add(TEXT("ShopInteractionPoint"));
        return Volume;
    };
    SupplyPort=CreateDefaultSubobject<UHoseSocketComponent>(TEXT("SupplyPort"));
    SupplyPort->SetupAttachment(ContainerRoot);
    SupplyPort->SetRelativeLocation(FVector(0,0,120));
    SupplyPort->SetRelativeRotation(FRotationMatrix::MakeFromZ(FVector::ForwardVector).Rotator());
    SupplyPort->bEditableWhenInherited=true;
    SupplyPort->CapacityUnits=200.f;
    SupplyPort->bCanSupply=true;
    SupplyPort->bCanReceive=true;
    SupplyPort->bUseSignedPressure=true;
    SupplyPort->bSupplyEnabled=false; SupplyPort->bRequestsFlow=false; SupplyPort->bAllowManagedContentBridge=true;
    OutletJet=CreateDefaultSubobject<UNiagaraComponent>(TEXT("OutletJet"));
    OutletJet->SetupAttachment(SupplyPort);
    OutletJet->SetAutoActivate(false);
    ValvePivot=CreateDefaultSubobject<USceneComponent>(TEXT("ValvePivot")); ValvePivot->SetupAttachment(ContainerRoot);
    ValvePivot->SetRelativeLocation(FVector(0,0,175));
    ValveVolume=MakeControlVolume(TEXT("ValveVolume"),ValvePivot,FVector(28,28,9));
    ValveRotor=CreateDefaultSubobject<USceneComponent>(TEXT("ValveRotor")); ValveRotor->SetupAttachment(ValvePivot);
    ValveWheel=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ValveWheel")); ValveWheel->SetupAttachment(ValveRotor);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    ValveWheel->SetStaticMesh(Mesh.Object); ValveWheel->SetRelativeScale3D(FVector(.45,.45,.06));
    ValveWheel->SetCollisionEnabled(ECollisionEnabled::QueryOnly); ValveWheel->SetCollisionResponseToAllChannels(ECR_Ignore);
    ValveWheel->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); ValveWheel->ComponentTags.Add(TEXT("ColaContainerValve"));
    // A visible spoke makes rotation readable even with the cylindrical fallback wheel.
    auto* Spoke=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ValveSpoke")); Spoke->SetupAttachment(ValveRotor);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Spoke->SetStaticMesh(Cube.Object); Spoke->SetRelativeScale3D(FVector(.5,.045,.07)); Spoke->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SupplyDisplay=CreateDefaultSubobject<UTextRenderComponent>(TEXT("SupplyDisplay")); SupplyDisplay->SetupAttachment(ContainerRoot);
    SupplyDisplay->SetRelativeLocation(FVector(30,0,100)); SupplyDisplay->SetWorldSize(8); SupplyDisplay->SetHorizontalAlignment(EHTA_Center);
    SupplyDisplay->SetTextRenderColor(FColor(255,210,100));
}
void APressurizedColaContainer::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    DisplayedValveAngle=FMath::Clamp(ValveOpening,0.f,1.f)*FMath::Max(1.f,ValveTravelDegrees);
    ValveRotor->SetRelativeRotation(FRotator(0,DisplayedValveAngle,0));
    RefreshSupplySettings(); UpdateDisplay();
}
void APressurizedColaContainer::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority())
    {
        const float Capacity=FMath::IsFinite(SupplyPort->CapacityUnits)?FMath::Max(0.f,SupplyPort->CapacityUnits):0.f;
        SupplyPort->CapacityUnits=Capacity;
        SupplyPort->SetStoredUnits(FMath::Clamp(FMath::IsFinite(InitialUnits)?InitialUnits:0.f,0.f,Capacity));
        if(auto* Manager=APropManager::Get(this)) SupplyPort->ContentCreationTime=Manager->WorldElapsedSeconds;
        PreviousSuppliedUnits=SupplyPort->TotalSuppliedUnits; RefreshSupplySettings();
    }
    UpdateDisplay();
}
UResourceTransferEndpointComponent* APressurizedColaContainer::GetResourceTransferEndpoint_Implementation() const { return SupplyPort; }
bool APressurizedColaContainer::IsValveControl(const UPrimitiveComponent* Component) const
{
    return Component && (Component == ValveVolume || Component == ValveWheel);
}
bool APressurizedColaContainer::IsHoseSocket(const UPrimitiveComponent* Component) const
{
    return Component && SupplyPort && Component==SupplyPort->GetTraceVolume();
}
void APressurizedColaContainer::BeginContainerInteraction(APawn* Player, UPrimitiveComponent* HittingComponent)
{
    if (!HasAuthority() || !IsValid(Player)) return;
    if (auto* Actions = Player->FindComponentByClass<UShopPlayerInteractionComponent>())
        Actions->ExecuteColaContainerInteraction(this,HittingComponent);
}
bool APressurizedColaContainer::AttachSupplyHose(AHoseCableActor* Hose)
{
    return IsValid(SupplyPort) && SupplyPort->AttachHose(Hose);
}
bool APressurizedColaContainer::AttachSupplyHoseAtConnector(AHoseCableActor* Hose, int32 PointIndex)
{
    return IsValid(SupplyPort) && SupplyPort->AttachHose(Hose, PointIndex);
}
float APressurizedColaContainer::GetRemainingUnits() const { return SupplyPort?SupplyPort->GetStoredUnits():0.f; }
void APressurizedColaContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APressurizedColaContainer,OutletPressure); DOREPLIFETIME(APressurizedColaContainer,OutletTemperature);
    DOREPLIFETIME(APressurizedColaContainer,MaxOutletUnitsPerSecond); DOREPLIFETIME(APressurizedColaContainer,ValveOpening);
    DOREPLIFETIME(APressurizedColaContainer,TotalDispensedUnits); DOREPLIFETIME(APressurizedColaContainer,CurrentOutletUnitsPerSecond);
    DOREPLIFETIME(APressurizedColaContainer,OpenOutletUnitsPerSecond);
}
void APressurizedColaContainer::RefreshSupplySettings()
{
    if(!HasAuthority() || GetNetMode()==NM_Client || !SupplyPort) return;
    const float Open=FMath::IsFinite(ValveOpening)?FMath::Clamp(ValveOpening,0.f,1.f):0.f;
    const bool Valid=FMath::IsFinite(OutletPressure) && FMath::IsFinite(OutletTemperature)
        && FMath::IsFinite(MaxOutletUnitsPerSecond) && MaxOutletUnitsPerSecond>0;
    const bool ValveOpen=Valid && Open>UE_SMALL_NUMBER;
    const float Pressure=ValveOpen ? OutletPressure*Open : 0.f;
    SupplyPort->bSupplyEnabled=ValveOpen && Pressure>=0.f;
    SupplyPort->bRequestsFlow=ValveOpen && SupplyPort->bCanReceive;
    SupplyPort->PushPressure=FMath::Max(0.f,Pressure);
    SupplyPort->PullPressure=FMath::Max(0.f,-Pressure);
    SupplyPort->SetOperatingPressure(Pressure);
    SupplyPort->MaxSupplyUnitsPerSecond=Valid?MaxOutletUnitsPerSecond*Open:0.f;
    SupplyPort->ContentTemperature=FMath::IsFinite(OutletTemperature)?OutletTemperature:20.f;
}
bool APressurizedColaContainer::SetValveOpening(float Opening)
{
    if(!HasAuthority() || GetNetMode()==NM_Client || !FMath::IsFinite(Opening)) return false;
    ValveOpening=FMath::Clamp(Opening,0.f,1.f); RefreshSupplySettings(); ForceNetUpdate(); return true;
}
bool APressurizedColaContainer::RotateValve(float Degrees)
{
    return FMath::IsFinite(Degrees) && FMath::IsFinite(ValveTravelDegrees) && ValveTravelDegrees>0
        && SetValveOpening(ValveOpening+Degrees/ValveTravelDegrees);
}
void APressurizedColaContainer::OnRep_ValveOpening() { UpdateDisplay(); }
void APressurizedColaContainer::UpdateDisplay()
{
    if(!SupplyDisplay) return;
    SupplyDisplay->SetText(FText::FromString(FString::Printf(TEXT("COLA %d | %.1f / %.1f\nValve %.0f%% | %.1f pressure | %.0f C\nFlow %.1f /s | Out %.1f\nE: open   Shift+E: close"),
        SupplyPort->ResourceID,HasActorBegunPlay()?GetRemainingUnits():InitialUnits,SupplyPort->CapacityUnits,
        ValveOpening*100.f,SupplyPort->GetOperatingPressure(),OutletTemperature,
        IsValid(SupplyPort->ConnectedHose) ? SupplyPort->GetSignedFlowUnitsPerSecond() : CurrentOutletUnitsPerSecond,TotalDispensedUnits)));
}

void APressurizedColaContainer::DrainOpenOutlet(float DeltaSeconds)
{
    const float PreviousRate=OpenOutletUnitsPerSecond;
    OpenOutletUnitsPerSecond=0.f;
    if (SupplyPort->bCanSupply && SupplyPort->bSupplyEnabled && !SupplyPort->bIsBroken
        && SupplyPort->Category==EResourceTransportCategory::Content && SupplyPort->PushPressure>0.f
        && !IsValid(SupplyPort->ConnectedHose) && !SupplyPort->HasConnectedSupplyHose())
    {
        const float Stored=SupplyPort->GetStoredUnits();
        const float Rate=SupplyPort->MaxSupplyUnitsPerSecond;
        const float Remaining=float(FMath::Max(0., double(Stored)-double(Rate)*DeltaSeconds));
        if (Remaining<Stored && SupplyPort->SetStoredUnits(Remaining))
        {
            SupplyPort->TotalSuppliedUnits+=Stored-Remaining;
            if (Remaining>0.f) OpenOutletUnitsPerSecond=Rate;
        }
    }
    if (OpenOutletUnitsPerSecond!=PreviousRate) ForceNetUpdate();
}

void APressurizedColaContainer::UpdateOutletJet()
{
    if (GetNetMode()==NM_DedicatedServer || !OutletJet->GetAsset()) return;
    if (OpenOutletUnitsPerSecond<=0.f || SupplyPort->PushPressure<=0.f || IsValid(SupplyPort->ConnectedHose))
    {
        if (bOutletJetEmitting) OutletJet->Deactivate();
        bOutletJetEmitting=false;
        return;
    }

    static const FName FlowParameter(TEXT("User.OutletFlow"));
    static const FName PressureParameter(TEXT("User.OutletPressure"));
    // Bound the cosmetic emitter budget if a designer sets an unusually high gameplay rating.
    OutletJet->SetVariableFloat(FlowParameter,FMath::Min(OpenOutletUnitsPerSecond,16.f));
    OutletJet->SetVariableFloat(PressureParameter,FMath::Min(SupplyPort->PushPressure,24.f));
    if (!bOutletJetEmitting)
    {
        OutletJet->Activate(true);
        bOutletJetEmitting=true;
    }
}

void APressurizedColaContainer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
    if(HasAuthority())
    {
        RefreshSupplySettings();
        DrainOpenOutlet(DeltaSeconds);
        TotalDispensedUnits=SupplyPort->TotalSuppliedUnits;
    }
    UpdateOutletJet();
    const float Goal=ValveOpening*FMath::Max(1.f,ValveTravelDegrees);
    DisplayedValveAngle=FMath::FInterpConstantTo(DisplayedValveAngle,Goal,DeltaSeconds,FMath::Max(1.f,ValveVisualDegreesPerSecond));
    ValveRotor->SetRelativeRotation(FRotator(0,DisplayedValveAngle,0));
    DisplayAccumulator+=DeltaSeconds;
    if(DisplayAccumulator>=.1f)
    {
        if(HasAuthority())
        {
            CurrentOutletUnitsPerSecond=FMath::Max(0.f,TotalDispensedUnits-PreviousSuppliedUnits)/DisplayAccumulator;
            PreviousSuppliedUnits=TotalDispensedUnits;
        }
        DisplayAccumulator=0; UpdateDisplay();
    }
}
