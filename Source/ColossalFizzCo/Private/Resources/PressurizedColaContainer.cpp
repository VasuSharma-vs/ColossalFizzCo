#include "Resources/PressurizedColaContainer.h"
#include "Components/ResourceTransferEndpointComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Props/InteractablePropManager.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

APressurizedColaContainer::APressurizedColaContainer()
{
    bReplicates=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=true;
    ContainerRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ContainerRoot")); SetRootComponent(ContainerRoot);
    SupplyPort=CreateDefaultSubobject<UResourceTransferEndpointComponent>(TEXT("SupplyPort")); SupplyPort->SetupAttachment(ContainerRoot);
    SupplyPort->SetRelativeLocation(FVector(0,0,120)); SupplyPort->CapacityUnits=200;
    SupplyPort->MaxConnectedHoses=1;
    SupplyPort->bCanReceive=false; SupplyPort->bSupplyEnabled=false; SupplyPort->bAllowManagedContentBridge=true;
    ValvePivot=CreateDefaultSubobject<USceneComponent>(TEXT("ValvePivot")); ValvePivot->SetupAttachment(SupplyPort);
    ValvePivot->SetRelativeLocation(FVector(0,0,25));
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
float APressurizedColaContainer::GetRemainingUnits() const { return SupplyPort?SupplyPort->GetStoredUnits():0.f; }
void APressurizedColaContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APressurizedColaContainer,OutletPressure); DOREPLIFETIME(APressurizedColaContainer,OutletTemperature);
    DOREPLIFETIME(APressurizedColaContainer,MaxOutletUnitsPerSecond); DOREPLIFETIME(APressurizedColaContainer,ValveOpening);
    DOREPLIFETIME(APressurizedColaContainer,TotalDispensedUnits); DOREPLIFETIME(APressurizedColaContainer,CurrentOutletUnitsPerSecond);
}
void APressurizedColaContainer::RefreshSupplySettings()
{
    if(!HasAuthority() || GetNetMode()==NM_Client || !SupplyPort) return;
    const float Open=FMath::IsFinite(ValveOpening)?FMath::Clamp(ValveOpening,0.f,1.f):0.f;
    const bool Valid=FMath::IsFinite(OutletPressure) && OutletPressure>=0 && FMath::IsFinite(OutletTemperature)
        && FMath::IsFinite(MaxOutletUnitsPerSecond) && MaxOutletUnitsPerSecond>0;
    SupplyPort->bSupplyEnabled=Valid && Open>UE_SMALL_NUMBER;
    SupplyPort->PushPressure=Valid?OutletPressure*Open:0.f;
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
        ValveOpening*100.f,SupplyPort->PushPressure,OutletTemperature,CurrentOutletUnitsPerSecond,TotalDispensedUnits)));
}
void APressurizedColaContainer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0) return;
    if(HasAuthority())
    {
        RefreshSupplySettings(); TotalDispensedUnits=SupplyPort->TotalSuppliedUnits;
    }
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
