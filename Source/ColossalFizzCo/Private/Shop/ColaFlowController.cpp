#include "Shop/ColaFlowController.h"

#include "Components/BoxComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Props/InteractablePropManager.h"
#include "Resources/HoseCableActor.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	const FName GeneratedFlowSocket(TEXT("GeneratedFlowSocket"));
	constexpr int32 PortsPerRow=4;
	constexpr float PortSpacing=24.f;

	bool IsLeverOperator(const APawn* Player, const UWorld* World)
	{
		const auto* PC=IsValid(Player) ? Cast<APlayerController>(Player->GetController()) : nullptr;
		return PC && PC->GetPawn()==Player && Player->GetWorld()==World;
	}

	FTransform DefaultOutletPosition(int32 Index)
	{
		const double X=(Index%PortsPerRow-(PortsPerRow-1)*.5)*PortSpacing;
		const double Z=(Index/PortsPerRow)*24.-12.;
		return FTransform(FRotationMatrix::MakeFromZ(FVector(0,1,0)).ToQuat(),FVector(X,0,Z));
	}

	void ConfigureControlCollision(UStaticMeshComponent& Control)
	{
		Control.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Control.SetCollisionResponseToAllChannels(ECR_Ignore);
		Control.SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
		Control.SetGenerateOverlapEvents(false);
		Control.ComponentTags.Add(TEXT("ShopInteractionPoint"));
	}
}

UFlowControllerOutputMountComponent::UFlowControllerOutputMountComponent()
{
	ArrowColor=FColor(112,225,199);
	ArrowSize=.65f;
	bEditableWhenInherited=true;
	SetHiddenInGame(true);
	EnsureOutletPositions(AColaFlowController::MaxOutletPorts);
}

void UFlowControllerOutputMountComponent::EnsureOutletPositions(int32 Count)
{
	const int32 PreviousCount=OutletPositions.Num();
	if (PreviousCount>=Count) return;
	OutletPositions.SetNum(Count);
	for (int32 Index=PreviousCount; Index<Count; ++Index)
		OutletPositions[Index]=DefaultOutletPosition(Index);
}

#if WITH_EDITOR
void UFlowControllerOutputMountComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName PositionsName=GET_MEMBER_NAME_CHECKED(UFlowControllerOutputMountComponent,OutletPositions);
	const bool bMovedOutlet=Event.GetMemberPropertyName()==PositionsName || Event.GetPropertyName()==PositionsName;
	TWeakObjectPtr<AColaFlowController> Controller=Cast<AColaFlowController>(GetOwner());
	Super::PostEditChangeProperty(Event);
	if (auto* Flow=Controller.Get(); bMovedOutlet && Flow && !Flow->IsTemplate() && Flow->GetWorld() && !Flow->GetWorld()->IsGameWorld())
		Flow->RerunConstructionScripts();
}
#endif

AColaFlowController::AColaFlowController()
{
	bReplicates=true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick=true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ControllerRoot")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ControllerMesh(TEXT("/Game/Assest/meshs/FillingLeaver/Meshy_Crimson_Steam_Engine_Mesh_0.Meshy_Crimson_Steam_Engine_Mesh_0"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LeverMesh(TEXT("/Game/Assest/meshs/FillingLeaver/Meshy_Crimson_Steam_Engine_Mesh_0_001.Meshy_Crimson_Steam_Engine_Mesh_0_001"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> UpgradeMaterial(TEXT("/Game/Assest/Blueprints/Cola_Container/M_ColaValveBrass.M_ColaValveBrass"));
	SocketMesh=Cylinder.Object;
	UpgradeDefinitions=TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Assest/Blueprints/FillingMachine/Upgrades/DT_FlowControllerUpgrades.DT_FlowControllerUpgrades")));

	Housing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(GetRootComponent());
	Housing->SetStaticMesh(ControllerMesh.Object);
	Housing->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	Lever=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ManualFillingLever"));
	Lever->SetupAttachment(Housing);
	Lever->SetStaticMesh(LeverMesh.Object);
	Lever->SetRelativeLocation(FVector(-7.37939,19,52));
	Lever->SetRelativeRotation(FRotator(0,0,-10));
	ConfigureControlCollision(*Lever);

	SelectorDial=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectorDial"));
	SelectorDial->SetupAttachment(Housing);
	SelectorDial->SetStaticMesh(Cylinder.Object);
	SelectorDial->SetRelativeLocation(FVector(24.2822,-40.793903,85));
	SelectorDial->SetRelativeRotation(FRotator(0,0,90));
	SelectorDial->SetRelativeScale3D(FVector(.26,.26,.06));
	ConfigureControlCollision(*SelectorDial);

	SelectorPointer=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectorPointer"));
	SelectorPointer->SetupAttachment(SelectorDial);
	SelectorPointer->SetStaticMesh(Cube.Object);
	SelectorPointer->SetRelativeLocation(FVector(0,30,55));
	SelectorPointer->SetRelativeScale3D(FVector(.06,.4,.08));
	SelectorPointer->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UpgradeControl=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpgradeControl"));
	UpgradeControl->SetupAttachment(Housing);
	UpgradeControl->SetStaticMesh(Cylinder.Object);
	UpgradeControl->SetMaterial(0,UpgradeMaterial.Object);
	UpgradeControl->SetRelativeLocation(FVector(-28,-48,77));
	UpgradeControl->SetRelativeRotation(FRotator(0,0,90));
	UpgradeControl->SetRelativeScale3D(FVector(.22,.22,.16));
	ConfigureControlCollision(*UpgradeControl);

	UpgradeLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("UpgradeLabel"));
	UpgradeLabel->SetupAttachment(Housing);
	UpgradeLabel->SetRelativeLocation(FVector(-28,-59,92));
	UpgradeLabel->SetRelativeRotation(FRotationMatrix::MakeFromXZ(FVector(0,-1,0),FVector::UpVector).Rotator());
	UpgradeLabel->SetHorizontalAlignment(EHTA_Center);
	UpgradeLabel->SetTextRenderColor(FColor(255,216,83));
	UpgradeLabel->SetWorldSize(7.f);
	UpgradeLabel->SetText(NSLOCTEXT("FlowController","UpgradesLabel","UPGRADES"));

	SupplyInputVolume=CreateDefaultSubobject<UBoxComponent>(TEXT("SupplyPort"));
	SupplyInputVolume->SetupAttachment(Housing);
	SupplyInputVolume->SetRelativeLocation(FVector(0,-54.793903,39));
	SupplyInputVolume->SetRelativeScale3D(FVector(1,1,1.395254));
	SupplyInputVolume->SetBoxExtent(FVector(25,25,45));
	SupplyInputVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SupplyInputVolume->SetGenerateOverlapEvents(false);
	SupplyInputVolume->SetHiddenInGame(true);

	SupplyOutputMount=CreateDefaultSubobject<UFlowControllerOutputMountComponent>(TEXT("SupplyOutputMount"));
	SupplyOutputMount->SetupAttachment(Housing);
	SupplyOutputMount->SetRelativeLocation(FVector(0,49,42));
}

void AColaFlowController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	DialRestRotation=SelectorDial->GetRelativeRotation().Quaternion();
	RebuildSockets();
	UpdateSelectorDial();
}

void AColaFlowController::BeginPlay()
{
	Super::BeginPlay();
	if (AllSelectorSockets.IsEmpty() || AllFillerSockets.IsEmpty()) RebuildSockets();
	if (HasAuthority())
	{
		for (TActorIterator<APropManager> It(GetWorld()); It; ++It)
		{
			if (const auto* Saved=It->FindFlowControllerUpgrades(*this)) RestoreUpgradeState(*Saved);
			break;
		}
		UpdateFlow();
	}
	UpdateSelectorDial();
}

void AColaFlowController::EndPlay(EEndPlayReason::Type Reason)
{
	if (HasAuthority()) ReleaseBatch();
	Super::EndPlay(Reason);
}

void AColaFlowController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		UpdateMachinePlacement();
		UpdateBatch();
		UpdateFlow();
		SampleFlowRates(DeltaSeconds);
	}
	FRotator Rotation=Lever->GetRelativeRotation();
	const float Speed=110.f/FMath::Max(.05f,LeverTravelSeconds);
	Rotation.Roll=FMath::FInterpConstantTo(float(Rotation.Roll),bLeverHeld ? 100.f : -10.f,DeltaSeconds,Speed);
	Lever->SetRelativeRotation(Rotation);
}

void AColaFlowController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AColaFlowController,UpgradeState);
	DOREPLIFETIME(AColaFlowController,SelectedInputIndex);
	DOREPLIFETIME(AColaFlowController,SelectedContentID);
	DOREPLIFETIME(AColaFlowController,PortionsPerSecondPerFiller);
	DOREPLIFETIME(AColaFlowController,ConnectedOutletCount);
	DOREPLIFETIME(AColaFlowController,InletSupplyPressure);
	DOREPLIFETIME(AColaFlowController,PressurePerOutlet);
	DOREPLIFETIME(AColaFlowController,InletPortionsPerSecond);
	DOREPLIFETIME(AColaFlowController,OutletPortionsPerSecond);
	DOREPLIFETIME(AColaFlowController,bLeverHeld);
	DOREPLIFETIME(AColaFlowController,bFlowPaused);
	DOREPLIFETIME(AColaFlowController,ActiveFillerCount);
	DOREPLIFETIME(AColaFlowController,bBatchSafeToRelease);
	DOREPLIFETIME(AColaFlowController,LeverOperator);
	DOREPLIFETIME(AColaFlowController,PlacementOperator);
	DOREPLIFETIME(AColaFlowController,bMachinePlacementActive);
}

void AColaFlowController::RebuildSockets()
{
	TInlineComponentArray<USceneComponent*> Existing(this);
	for (auto* Component:Existing)
		if (IsValid(Component) && Component->ComponentHasTag(GeneratedFlowSocket)) Component->DestroyComponent();

	RefreshOutletPositions();
	// Clients need the same named components even when a hose replicates before an upgrade.
	BuildBank(AllSelectorSockets,SelectorMounts,MaxInletPorts,true);
	BuildBank(AllFillerSockets,SupplyOutputMount->OutletPositions,MaxOutletPorts,false);
	OnRep_UpgradeState();
}

void AColaFlowController::RefreshOutletPositions()
{
	const int32 LegacyCount=FMath::Min(FillerMounts.Num(),MaxOutletPorts);
	SupplyOutputMount->EnsureOutletPositions(MaxOutletPorts);
	if (!LegacyCount) return;
	const FTransform MountInActor=SupplyOutputMount->GetRelativeTransform()*Housing->GetRelativeTransform();
	for (int32 Index=0; Index<LegacyCount; ++Index)
	{
		FTransform& Position=SupplyOutputMount->OutletPositions[Index];
		if (Position.Equals(DefaultOutletPosition(Index)))
			Position=FillerMounts[Index].GetRelativeTransform(MountInActor);
	}
	FillerMounts.Reset();
}

void AColaFlowController::BuildBank(TArray<TObjectPtr<UHoseSocketComponent>>& Bank, const TArray<FTransform>& Mounts, int32 Count, bool bInlet)
{
	Bank.Reset(Count);
	for (int32 Index=0;Index<Count;++Index)
	{
		const FName Name=*FString::Printf(TEXT("%sSocket_%02d"),bInlet ? TEXT("Selector") : TEXT("Filler"),Index+1);
		auto* Socket=NewObject<UHoseSocketComponent>(this,Name,RF_Transactional|RF_Transient);
		Socket->CreationMethod=EComponentCreationMethod::UserConstructionScript;
		Socket->ComponentTags.Add(GeneratedFlowSocket);
		const bool bHasMountOverride=Mounts.IsValidIndex(Index);
		const bool bUseSupplyVolume=bInlet && !bHasMountOverride;
		USceneComponent* MountParent=SupplyOutputMount.Get();
		if (bInlet) MountParent=bUseSupplyVolume ? SupplyInputVolume.Get() : GetRootComponent();
		Socket->SetupAttachment(MountParent);
		const int32 Row=Index/PortsPerRow;
		const int32 RowCount=FMath::Min(PortsPerRow,Count-Row*PortsPerRow);
		const double X=(Index%PortsPerRow-(RowCount-1)*.5)*PortSpacing;
		const FTransform DefaultInlet(FRotationMatrix::MakeFromZ(FVector(0,-1,0)).ToQuat(),FVector(X,4,Row*18.));
		const FTransform& Mount=bHasMountOverride ? Mounts[Index] : DefaultInlet;
		Socket->SetRelativeTransform(Mount);
		if (bUseSupplyVolume) Socket->SetAbsolute(false,false,true);
		Socket->TraceBoxExtent=FVector(9,9,10);
		Socket->TraceBoxOffset=FVector(0,0,7);
		Socket->bCanSupply=!bInlet;
		Socket->bCanReceive=bInlet;
		Socket->bSupplyEnabled=false;
		Socket->bRequestsFlow=false;
		Socket->bUseSignedPressure=!bInlet;
		Socket->bUseFixedHoseDirection=true;
		Socket->bAcceptAnyContentWhenEmpty=bInlet;
		Socket->bAllowManagedContentBridge=true;
		Socket->MaxIntakeUnitsPerSecond=MaximumSocketFlowPortionsPerSecond;
		Socket->MaxOuttakeUnitsPerSecond=MaximumSocketFlowPortionsPerSecond;
		if (!bInlet) Socket->ResourceID=0;
		Socket->CapacityUnits=bInlet ? FMath::Max(.25f,InletBufferPortions) : 0.f;
		Socket->MaxReceiveUnitsPerSecond=bInlet ? GetIntakeRate() : 0.f;
		Socket->SetNetAddressable();
		Socket->RegisterComponent();
		Bank.Add(Socket);

		auto* Collar=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("%sCollar"),*Name.ToString()),RF_Transient);
		Collar->CreationMethod=EComponentCreationMethod::UserConstructionScript;
		Collar->ComponentTags.Add(GeneratedFlowSocket);
		Collar->SetupAttachment(Socket);
		Collar->SetStaticMesh(SocketMesh);
		Collar->SetRelativeScale3D(FVector(.17,.17,.13));
		Collar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Collar->SetNetAddressable();
		Collar->RegisterComponent();
		const FColor BankColor=bInlet ? FColor(242,188,112) : FColor(112,225,199);
		if (auto* Material=Collar->CreateDynamicMaterialInstance(0))
			Material->SetVectorParameterValue(TEXT("Color"),FLinearColor(BankColor));

		auto* Label=NewObject<UTextRenderComponent>(this,*FString::Printf(TEXT("%sLabel"),*Name.ToString()),RF_Transient);
		Label->CreationMethod=EComponentCreationMethod::UserConstructionScript;
		Label->ComponentTags.Add(GeneratedFlowSocket);
		Label->SetupAttachment(Socket);
		if (bUseSupplyVolume) Label->SetAbsolute(false,false,true);
		const FVector Outward=Mount.GetUnitAxis(EAxis::Z);
		const FTransform LabelInMount(FRotationMatrix::MakeFromXZ(Outward,FVector::UpVector).ToQuat(),
			Mount.GetLocation()+10.*Outward+FVector(0,0,13));
		Label->SetRelativeTransform(LabelInMount.GetRelativeTransform(Mount));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetTextRenderColor(BankColor);
		Label->SetWorldSize(8.f);
		Label->SetText(FText::FromString(FString::Printf(TEXT("%s %d"),bInlet ? TEXT("IN") : TEXT("OUT"),Index+1)));
		Label->SetNetAddressable();
		Label->RegisterComponent();
	}
}

void AColaFlowController::OnRep_UpgradeState()
{
	SelectorSocketCount=1+UpgradeState.InputPortsRank;
	FillerSocketCount=1+UpgradeState.OutputPortsRank;
	LeverLevel=1+UpgradeState.LeverRank;
	if (HasAuthority()) SelectedInputIndex=FMath::Clamp(SelectedInputIndex,0,SelectorSocketCount-1);
	UpdateSocketAvailability();
	UpdateSelectorDial();
}

void AColaFlowController::UpdateSocketAvailability()
{
	const auto SetUnlockedPorts=[this](const auto& Sockets, auto& Unlocked, int32 Count, bool bInlet)
	{
		Unlocked.Reset(Count);
		for (int32 Index=0; Index<Sockets.Num(); ++Index)
		{
			auto* Socket=Sockets[Index].Get();
			const bool bUnlocked=Index<Count;
			if (!bUnlocked && HasAuthority() && Socket->ConnectedHose) Socket->DetachHose();
			const bool bAvailable=bUnlocked && !bMachinePlacementActive;
			Socket->bCanReceive=bAvailable && bInlet;
			Socket->bCanSupply=bAvailable && !bInlet;
			Socket->SetVisibility(bUnlocked,true);
			if (auto* Trace=Socket->GetTraceVolume())
				Trace->SetCollisionEnabled(bUnlocked ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			if (bUnlocked) Unlocked.Add(Socket);
		}
	};
	SetUnlockedPorts(AllSelectorSockets,SelectorSockets,SelectorSocketCount,true);
	SetUnlockedPorts(AllFillerSockets,FillerSockets,FillerSocketCount,false);
}

bool AColaFlowController::SelectInput(int32 Index)
{
	if (!HasAuthority() || bMachinePlacementActive || !SelectorSockets.IsValidIndex(Index) || bLeverHeld) return false;
	SelectedInputIndex=Index;
	UpdateFlow();
	UpdateSelectorDial();
	ForceNetUpdate();
	return true;
}

bool AColaFlowController::CanReachControl(const APawn* Player, const UPrimitiveComponent& Control, float Reach) const
{
	return IsLeverOperator(Player,GetWorld())
		&& FMath::IsFinite(Reach) && Reach>0.f
		&& Control.Bounds.GetBox().ComputeSquaredDistanceToPoint(Player->GetActorLocation())<=FMath::Square(Reach);
}

bool AColaFlowController::CanUseControl(const APawn* Player, const UPrimitiveComponent& Control, float Reach) const
{
	if (!HasAuthority() || bMachinePlacementActive || !CanReachControl(Player,Control,Reach)) return false;
	FVector Eye; FRotator View;
	Player->GetActorEyesViewPoint(Eye,View);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(FlowControllerControl),false,Player);
	return !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Control.Bounds.Origin,ECC_Visibility,Query) || Hit.GetActor()==this;
}

bool AColaFlowController::UseSelector(const APawn* Player, float Reach)
{
	if (SelectorSockets.IsEmpty() || !CanUseControl(Player,*SelectorDial,Reach)) return false;
	return SelectInput((SelectedInputIndex+1)%SelectorSockets.Num());
}

bool AColaFlowController::IsLeverHeldBy(const APawn* Player) const
{
	return bLeverHeld && IsValid(Player) && LeverOperator.Get()==Player;
}

bool AColaFlowController::IsDrivingTable(const AManualColaFillingMachine& Table, const AActor* Player) const
{
	if (!IsLeverHeldBy(Cast<APawn>(Player))) return false;
	const auto* Feed=Table.GetHoseSocket(0);
	const auto* Hose=Feed ? Feed->ConnectedHose.Get() : nullptr;
	const auto* Outlet=IsValid(Hose) ? Cast<UHoseSocketComponent>(Hose->StartPort.Get()) : nullptr;
	return Outlet && FillerSockets.Contains(Outlet) && GetFillerTable(*Outlet)==&Table;
}

bool AColaFlowController::BeginLever(APawn* Player, float Reach)
{
	if (!CanUseControl(Player,*Lever,Reach)) return false;
	if (bLeverHeld) return UsesLatchedLever() && EndLever(Player);
	UpdateFlow();
	LeverOperator=Player;
	LeverReach=Reach;
	bLeverHeld=true;
	bFlowPaused=false;
	UpdateFlow();
	for (const UHoseSocketComponent* Outlet:FillerSockets)
	{
		auto* Table=GetFillerTable(*Outlet);
		if (Table && Table->Production->BeginFlowControllerHold(*this,*Player))
			ActiveStations.AddUnique(Table->Production.Get());
	}
	UpdateBatch();
	ForceNetUpdate();
	return true;
}

bool AColaFlowController::SetFlowPaused(const APawn* Player, bool bPaused)
{
	if (!HasAuthority() || !IsLeverHeldBy(Player) || !CanReachControl(Player,*Lever,LeverReach)) return false;
	if (bFlowPaused==bPaused) return true;
	bFlowPaused=bPaused;
	for (const auto& Entry:ActiveStations)
		if (auto* Station=Entry.Get()) Station->SetFlowControllerPaused(*this,bPaused);
	UpdateBatch();
	UpdateFlow();
	ForceNetUpdate();
	return true;
}

bool AColaFlowController::EndLever(const APawn* Player)
{
	if (!HasAuthority() || !IsLeverHeldBy(Player)) return false;
	ReleaseBatch();
	UpdateFlow();
	return true;
}

bool AColaFlowController::ReleaseLeverInput(const APawn* Player)
{
	return !UsesLatchedLever() && EndLever(Player);
}

void AColaFlowController::ReleaseBatch()
{
	if (!bLeverHeld) return;
	bLeverHeld=false;
	bFlowPaused=false;
	LeverOperator=nullptr;
	for (const auto& Entry:ActiveStations)
		if (auto* Station=Entry.Get()) Station->ReleaseFlowControllerHold(*this);
	ActiveStations.Reset();
	ActiveFillerCount=0;
	bBatchSafeToRelease=true;
	ForceNetUpdate();
}

void AColaFlowController::SampleFlowRates(float DeltaSeconds)
{
	FlowSampleSeconds+=DeltaSeconds;
	if (FlowSampleSeconds<.25f) return;

	double ReceivedUnits=0.;
	double SuppliedUnits=0.;
	// Only the selected inlet accepts flow. Sum the counters so a selector change
	// cannot mistake the new inlet's lifetime total for flow in the latest sample.
	for (const UHoseSocketComponent* Inlet:SelectorSockets) ReceivedUnits+=Inlet->TotalReceivedUnits;
	for (const UHoseSocketComponent* Outlet:FillerSockets) SuppliedUnits+=Outlet->TotalSuppliedUnits;

	InletPortionsPerSecond=float(FMath::Max(0.,ReceivedUnits-LastReceivedUnits)/FlowSampleSeconds);
	OutletPortionsPerSecond=float(FMath::Max(0.,SuppliedUnits-LastSuppliedUnits)/FlowSampleSeconds);
	LastReceivedUnits=ReceivedUnits;
	LastSuppliedUnits=SuppliedUnits;
	FlowSampleSeconds=0.f;
}

void AColaFlowController::UpdateSelectorDial()
{
	if (SelectorDial) SelectorDial->SetRelativeRotation(DialRestRotation
		*FQuat(FVector::UpVector,2.*PI*SelectedInputIndex/FMath::Max(1,SelectorSocketCount)));
}
