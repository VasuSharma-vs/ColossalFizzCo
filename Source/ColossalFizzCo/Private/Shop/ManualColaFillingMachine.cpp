#include "Shop/ManualColaFillingMachine.h"
#include "Shop/SocketColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/ColaSupplyContainer.h"
#include "Net/UnrealNetwork.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/ColaPropActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshPhysicsSimulationComponent.h"
#include "Components/HoseSocketComponent.h"
#include "Components/HoseTransferComponent.h"
#include "Resources/HoseCableActor.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/KismetMathLibrary.h"

AManualColaFillingMachine::AManualColaFillingMachine()
{
	bAutoConvertToISMIfAtRest=false; State=EPropState::Unsettled;
	PhysicsSimComponent->bUnsettleOnHit=false;
	bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); PrimaryActorTick.bStartWithTickEnabled=true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cube.Succeeded()) PropMesh->SetStaticMesh(Cube.Object);
	auto Visual=[&](const TCHAR* Name,FVector Position,FVector Scale,UStaticMesh* Mesh)
	{
		auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(Name); Part->SetupAttachment(PropMesh);
		Part->SetStaticMesh(Mesh); Part->SetRelativeLocation(Position); Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); return Part;
	};
	CradlePivot=CreateDefaultSubobject<USceneComponent>(TEXT("CradlePivot")); CradlePivot->SetupAttachment(PropMesh);
	CradlePivot->SetRelativeLocation(FVector(0,0,140)); CradlePivot->SetRelativeRotation(FRotator(RestCradlePitch,0,0));
	CradlePlatform=Visual(TEXT("CradlePlatform"),FVector(-38,0,0),FVector(.08,.8,2.1),Cube.Object); CradlePlatform->SetupAttachment(CradlePivot);
	auto CradleVisual=[&](const TCHAR* Name,FVector Position,FVector Scale,UStaticMesh* Mesh)
	{
		auto* Part=Visual(Name,Position,Scale,Mesh); Part->SetupAttachment(CradlePivot); return Part;
	};
	Visual(TEXT("NozzleSupport"),FVector(0,0,80),FVector(.15,.15,1.1),Cube.Object);
	CradleVisual(TEXT("NozzleArm"),FVector(-30,0,90),FVector(.65,.08,.08),Cube.Object);
	CradleVisual(TEXT("FillingNozzle"),FVector(0,0,98),FVector(.06,.06,.18),Cylinder.Object);
	CappingHead=CradleVisual(TEXT("CappingHead"),FVector(0,0,108),FVector(.2,.2,.06),Cylinder.Object);
	auto* CapTurnMark=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapTurnIndicator")); CapTurnMark->SetupAttachment(CappingHead);
	CapTurnMark->SetStaticMesh(Cube.Object); CapTurnMark->SetRelativeLocation(FVector(0,30,55)); CapTurnMark->SetRelativeScale3D(FVector(.1,.45,.08));
	CapTurnMark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CradleVisual(TEXT("NeckClampLeft"),FVector(-14,0,90),FVector(.06,.18,.10),Cube.Object);
	CradleVisual(TEXT("NeckClampRight"),FVector(14,0,90),FVector(.06,.18,.10),Cube.Object);
	CradleVisual(TEXT("BottleHolderRing"),FVector(0,0,-87),FVector(.78,.78,.04),Cylinder.Object);
	AutoSealSwitch=Visual(TEXT("TemporarySealButton"),FVector(-35,-40,58),FVector(.16,.16,.08),Cylinder.Object);
	AutoSealSwitch->SetCollisionResponseToAllChannels(ECR_Ignore);
	AutoSealSwitch->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	AutoSealSwitch->SetGenerateOverlapEvents(false);
	AutoSealSwitch->ComponentTags.Add(TEXT("ShopInteractionPoint"));
	AutoSealLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("AutoSealLabel"));
	AutoSealLabel->SetupAttachment(AutoSealSwitch);
	AutoSealLabel->SetAbsolute(false,false,true);
	AutoSealLabel->SetRelativeLocation(FVector(0,-68,0));
	AutoSealLabel->SetRelativeRotation(FRotator(0,-90,0));
	AutoSealLabel->SetHorizontalAlignment(EHTA_Center);
	AutoSealLabel->SetWorldSize(8.f);
	Lever=Visual(TEXT("ManualFillingLever"),FVector(45,-35,125),FVector(.04,.04,.55),Cylinder.Object);
	Lever->SetRelativeRotation(FRotator(0,0,35));
	Pipe=Visual(TEXT("SingleLiquidSupplyPipe"),FVector::ZeroVector,FVector::OneVector,nullptr);
	Production=CreateDefaultSubobject<UShopProductionStationComponent>(TEXT("ManualProduction"));
	BottleFiller=CreateDefaultSubobject<UBottleFillingComponent>(TEXT("ManualBottleFiller"));
	BottleFiller->PortionsPerSecond=1; BottleFiller->MaxConnectionDistance=400; BottleFiller->TargetContentLevel=0;
	Production->Filler=BottleFiller; Production->bManualLeverMode=true; Production->bAllowSoloCap=true;
	Production->CapTrayCapacity=1; Production->SealTrayCapacity=1;
	Production->bRequiresCooperativeCap=false;
	Production->bOffersSupplies=false;
	ContentSelectorDial=Visual(TEXT("ContentSelectorDial"),FVector(0,-51,105),FVector(.26,.26,.06),Cylinder.Object);
	ContentSelectorDial->SetRelativeRotation(FRotator(0,0,90));
	auto* DialPointer=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContentDialPointer"));
	DialPointer->SetupAttachment(ContentSelectorDial); DialPointer->SetStaticMesh(Cube.Object);
	DialPointer->SetRelativeLocation(FVector(0,30,55)); DialPointer->SetRelativeScale3D(FVector(.06,.4,.08));
	DialPointer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	auto Box=[&](const TCHAR* Name,const TCHAR* Tag,FVector Position,FVector Extent)
	{
		auto* Part=CreateDefaultSubobject<UBoxComponent>(Name); Part->SetupAttachment(PropMesh);
		Part->ComponentTags.Add(Tag); Part->SetRelativeLocation(Position); Part->SetBoxExtent(Extent);
		Part->BodyInstance.bAutoWeld=false;
		Part->bEditableWhenInherited=true;
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); return Part;
	};
	BottleInputVolume=Box(TEXT("BottleInput"),TEXT("ShopBottleInput"),FVector(0,0,145),FVector(135,60,110));
	CapInputVolume=Box(TEXT("CapInput"),TEXT("ShopCapInput"),FVector(85,-60,70),FVector(80,80,35));
	SealInputVolume=Box(TEXT("SealInput"),TEXT("ShopSealInput"),FVector(-65,-25,70),FVector(30,40,35));
	Production->BottleInput=BottleInputVolume; Production->CapInput=CapInputVolume;
	Production->SealInput=SealInputVolume;
	auto TrayBase=[&](const TCHAR* Name,UBoxComponent* Volume,FVector Scale)
	{
		auto* Base=Visual(Name,FVector::ZeroVector,Scale,Cube.Object); Base->SetupAttachment(Volume);
		Base->BodyInstance.bAutoWeld=false;
		Base->SetRelativeLocation(FVector(0,0,-30)); Base->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Base->SetCollisionResponseToAllChannels(ECR_Block); Base->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
	};
	TrayBase(TEXT("CapTrayPlatform"),CapInputVolume,FVector(1.6,1.6,.06));
	TrayBase(TEXT("SealTrayPlatform"),SealInputVolume,FVector(.6,.8,.04));
	auto Point=[&](const TCHAR* Name,const TCHAR* Tag,FVector Position)
	{
		auto* Part=CreateDefaultSubobject<USceneComponent>(Name); Part->SetupAttachment(PropMesh);
		Part->ComponentTags.Add(Tag); Part->SetRelativeLocation(Position); return Part;
	};
	BottleHolderPoint=Point(TEXT("BottleHolder"),TEXT("ManualBottleHolder"),FVector(0,0,-89));
	BottleHolderPoint->SetupAttachment(CradlePivot);
	SupplyDeliveryPoint=Point(TEXT("SupplyOutput"),TEXT("ShopSupplyOutput"),FVector(-500,0,60));
	HoseConnections=Point(TEXT("HoseConnections"),TEXT("ManualHoseConnections"),FVector::ZeroVector);
	FlowControllerInlet=CreateDefaultSubobject<UHoseSocketComponent>(TEXT("FlowControllerInlet"));
	FlowControllerInlet->SetupAttachment(PropMesh);
	FlowControllerInlet->SetRelativeLocation(FVector(112,85,39));
	FlowControllerInlet->SetRelativeRotation(FRotationMatrix::MakeFromZ(FVector(1,0,0)).Rotator());
	FlowControllerInlet->TraceBoxExtent=FVector(13.f);
	FlowControllerInlet->TraceBoxOffset=FVector(0,0,7);
	FlowControllerInlet->ComponentTags.Add(TEXT("ShopInteractionPoint"));
	FlowInletCollar=Visual(TEXT("FlowInletCollar"),FVector::ZeroVector,FVector(.22,.22,.18),Cylinder.Object);
	FlowInletCollar->SetupAttachment(FlowControllerInlet);
	FlowInletCollar->SetRelativeLocation(FVector(0,0,4));
	Production->BottleHolder=BottleHolderPoint; Production->SupplyOutput=SupplyDeliveryPoint;
	Point(TEXT("ManualControls"),TEXT("ShopInteractionPoint"),FVector(0,-100,45));
	RefreshHosePortLayout();
}

void AManualColaFillingMachine::RefreshHosePortLayout()
{
	SupportedContentCount=FMath::Clamp(SupportedContentCount,1,32);
	const int32 PreviousCount=HosePortTransforms.Num();
	HosePortTransforms.SetNum(SupportedContentCount);
	HosePortContentIDs.SetNum(SupportedContentCount);
	for (int32 I=PreviousCount; I<SupportedContentCount; ++I)
		HosePortTransforms[I]=GetDefaultHosePortTransform(I);
}

FTransform AManualColaFillingMachine::GetDefaultHosePortTransform(int32 Index) const
{
	return FTransform(FRotator::ZeroRotator,FVector(52,(Index%4)*30.f-45.f,75+(Index/4)*30.f));
}
#if WITH_EDITOR
void AManualColaFillingMachine::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	RefreshHosePortLayout();
	Super::PostEditChangeProperty(Event);
}
#endif

void AManualColaFillingMachine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (CradlePivot) CradlePivot->SetRelativeRotation(FRotator(FMath::Lerp(RestCradlePitch,FillingCradlePitch,CradleAlpha),0,0));
	RebuildHoseSockets();
	UpdateControlLayout();
}

void AManualColaFillingMachine::RebuildHoseSockets()
{
	static const FName GeneratedTag(TEXT("GeneratedManualFillingHoseSocket"));
	static const FName LegacyPortTag(TEXT("ManualFillingHosePort"));
	RefreshHosePortLayout();
	TInlineComponentArray<USceneComponent*> Components(this);
	TArray<UHoseSocketComponent*> Authored;
	for (USceneComponent* Component : Components)
	{
		if (!IsValid(Component)) continue;
		const FString Name = Component->GetName();
		if (Component->CreationMethod == EComponentCreationMethod::UserConstructionScript
			&& (Component->ComponentHasTag(GeneratedTag) || Component->ComponentHasTag(LegacyPortTag)
				|| Name.StartsWith(TEXT("ReceivingHosePort_")) || Name.StartsWith(TEXT("HoseDirection_"))))
		{
			Component->DestroyComponent();
			continue;
		}
		if (auto* Socket = Cast<UHoseSocketComponent>(Component); Socket && Socket->GetAttachParent() == HoseConnections)
			Authored.Add(Socket);
	}
	Authored.Sort([](const UHoseSocketComponent& A, const UHoseSocketComponent& B) { return A.GetFName().LexicalLess(B.GetFName()); });
	const int32 PortCount=GetActiveHosePortCount();
	HoseSockets.Reset(PortCount);
	ConnectedNozzles.SetNum(PortCount);
	if (auto* Volume=FlowControllerInlet->GetTraceVolume())
		Volume->SetCollisionEnabled(bUseFlowController ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (bUseFlowController)
	{
		ConfigureReceivingHosePort(*FlowControllerInlet,true);
		HoseSockets.Add(FlowControllerInlet);
	}
	const int32 LegacyPortCount=bUseFlowController ? 0 : PortCount;
	for (int32 Index = 0; Index < LegacyPortCount; ++Index)
	{
		const bool bGenerated = !Authored.IsValidIndex(Index);
		auto* Socket = bGenerated ? NewObject<UHoseSocketComponent>(this, *FString::Printf(TEXT("GeneratedHoseSocket_%02d"), Index+1),
			RF_Transactional | RF_Transient) : Authored[Index];
		if (bGenerated)
		{
			Socket->CreationMethod = EComponentCreationMethod::UserConstructionScript;
			Socket->ComponentTags.Add(GeneratedTag);
			Socket->SetupAttachment(HoseConnections);
			FTransform Mount = HosePortTransforms[Index];
			Mount.SetRotation(Mount.GetRotation() * FQuat(FVector::RightVector, UE_HALF_PI));
			Socket->SetRelativeTransform(Mount);
			Socket->TraceBoxExtent = FVector(FMath::Max(2.f, HosePortRadius));
		}
		ConfigureReceivingHosePort(*Socket, bGenerated);
		Socket->SetNetAddressable();
		HoseSockets.Add(Socket);
		if (!Socket->IsRegistered()) Socket->RegisterComponent();
		if (auto* Volume = Socket->GetTraceVolume())
		{
			Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			if (bGenerated && HosePortTraceChannel != ECC_Visibility)
			{
				Volume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
				Volume->SetCollisionResponseToChannel(HosePortTraceChannel, ECR_Block);
			}
		}
	}
	for (int32 Index = LegacyPortCount; Index < Authored.Num(); ++Index)
		if (auto* Volume = Authored[Index]->GetTraceVolume()) Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool AManualColaFillingMachine::GetHosePortFromHit(const FHitResult& Hit,int32& PortIndex,FTransform& ConnectionTransform) const
{
	PortIndex=INDEX_NONE; ConnectionTransform=FTransform::Identity;
	if (Hit.GetActor()!=this) return false;
	const auto* Socket = UHoseSocketComponent::GetSocketFromHitComponent(Hit.GetComponent());
	if (!Socket) return false;
	PortIndex = HoseSockets.IndexOfByKey(Socket);
	if (PortIndex == INDEX_NONE) return false;
	ConnectionTransform = Socket->GetComponentTransform();
	return true;
}
FTransform AManualColaFillingMachine::GetHosePortTransform(int32 PortIndex) const
{
	const auto* Socket = GetHoseSocket(PortIndex);
	return Socket ? Socket->GetComponentTransform() : GetActorTransform();
}
bool AManualColaFillingMachine::AttachSupplyNozzle(const FHitResult& Hit,USceneComponent* Nozzle,int32 SourceISID,AActor* Player)
{
	int32 Index; FTransform Mount;
	if (!HasAuthority() || !IsValid(Nozzle) || Nozzle->GetWorld()!=GetWorld() || !GetHosePortFromHit(Hit,Index,Mount)) return false;
	if (const auto* Layout = Cast<ASocketColaFillingMachine>(this); Layout && Layout->IsMachinePlacementActive()) return false;
	auto* Port = GetHoseSocket(Index);
	if (!Port || Port->ConnectedHose || Port->GetReceivingHose() || Port->bIsBroken) return false;
	auto* Registry=APropManager::Get(this); auto* Source=Registry ? Registry->GetActiveActorFromISID(SourceISID) : nullptr;
	const int32 RequiredID=GetRequiredHoseContentID(Index);
	if (Source && RequiredID>0 && Source->LocalUniqueData.EventData.ContentID!=RequiredID) return false;
	if (!Source || Nozzle==Source->GetRootComponent() || !Nozzle->ComponentHasTag(TEXT("ManualFillingSupplyNozzle"))
		|| (Nozzle->GetOwner()!=Source && Nozzle->GetOwner()->GetOwner()!=Source) || !IsValid(Player)
		|| FVector::DistSquared(Player->GetActorLocation(),Nozzle->GetComponentLocation())>FMath::Square(400.f)
		|| Production->View.bLeverHeld || (ConnectedNozzles.IsValidIndex(Index) && IsValid(ConnectedNozzles[Index]) && ConnectedNozzles[Index]!=Nozzle)) return false;
	if (const auto* Parent=Nozzle->GetAttachParent(); Parent && Parent->GetOwner()!=Source && Parent->GetOwner()!=this) return false;
	for (int32 Other=0;Other<ConnectedNozzles.Num();++Other) if (Other!=Index && ConnectedNozzles[Other]==Nozzle) return false;
	if (ConnectedNozzles.Num()!=GetActiveHosePortCount()) ConnectedNozzles.SetNum(GetActiveHosePortCount());
	bool Attached=false;
	if (auto* Container=Cast<AColaSupplyContainer>(Source)) Attached=Container->MountHose(Port);
	else Attached=Nozzle->AttachToComponent(Port,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (!Attached) return false;
	ConnectedNozzles[Index]=Nozzle;
	if (Production->ExecuteForPlayer(Production->View.SessionID,EShopProductionAction::ConnectSupply,Index,SourceISID,Player)!=EShopCheckoutResult::Success)
	{ DisconnectSupplyNozzle(Index); return false; }
	ForceNetUpdate(); return true;
}

void AManualColaFillingMachine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AManualColaFillingMachine,ConnectedNozzles);
	DOREPLIFETIME(AManualColaFillingMachine,CradleAlpha);
}
bool AManualColaFillingMachine::HasSupplyNozzle(int32 PortIndex,int32 SourceISID) const
{
	if (SourceISID<=0) return GetSupplyHoseForPort(PortIndex)!=nullptr;
	const auto* Socket=GetHoseSocket(PortIndex);
	if (!ConnectedNozzles.IsValidIndex(PortIndex) || !IsValid(ConnectedNozzles[PortIndex]) || !Socket || Socket->bIsBroken) return false;
	const auto* Nozzle=ConnectedNozzles[PortIndex].Get(); const auto* Registry=APropManager::Get(this);
	const auto* Source=Registry ? Registry->GetActiveActorFromISID(SourceISID) : nullptr;
	return Source && Nozzle->GetAttachParent()==Socket && (Nozzle->GetOwner()==Source || Nozzle->GetOwner()->GetOwner()==Source);
}
void AManualColaFillingMachine::RestoreSupplyNozzles()
{
	if (!HasAuthority()) return;
	auto* Registry=APropManager::Get(this); const auto* Equipment=Registry ? Registry->FindUniqueData(ISID) : nullptr;
	if (!Equipment) return;
	const auto Connections=Equipment->EventData.ConnectedSupplyISIDs;
	ConnectedNozzles.SetNum(GetActiveHosePortCount());
	for (int32 I=0;I<Connections.Num() && I<HoseSockets.Num();++I)
	{
		auto* Socket = GetHoseSocket(I);
		if (!Socket || Socket->bIsBroken || Socket->ConnectedHose || Connections[I]<=0 || HasSupplyNozzle(I,Connections[I])) continue;
		const auto* SourceData=Registry->FindUniqueData(Connections[I]);
		if (!SourceData || SourceData->EventData.StoredInMachineISID!=ISID) continue;
		AActivePropActor* Active=Registry->GetActiveActorFromISID(Connections[I]);
		if (!Active) Registry->PromoteInstanceToASM(Connections[I],EPropActivationReason::LoadRestore,false,Active);
		if (auto* Container=Cast<AColaSupplyContainer>(Active); Container && Container->MountHose(Socket)) ConnectedNozzles[I]=Container->SupplyNozzle;
	}
}
void AManualColaFillingMachine::DisconnectSupplyNozzle(int32 PortIndex)
{
	if (!HasAuthority() || !ConnectedNozzles.IsValidIndex(PortIndex)) return;
	if (auto* Port=GetHoseSocket(PortIndex))
	{
		Port->SetFlowRequested(false);
		if (!Port->DetachHose())
			if (auto* Hose=Port->GetReceivingHose()) Hose->DisconnectEndpoints();
	}
	auto* Nozzle=ConnectedNozzles[PortIndex].Get(); ConnectedNozzles[PortIndex]=nullptr;
	if (!IsValid(Nozzle)) return;
	if (auto* Container=Cast<AColaSupplyContainer>(Nozzle->GetOwner())) Container->ReleaseHose();
	else Nozzle->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	ForceNetUpdate();
}

void AManualColaFillingMachine::BeginPlay()
{
	ContentDialRestRotation = ContentSelectorDial->GetRelativeRotation().Quaternion();
	if (HoseSockets.Num() != GetActiveHosePortCount() || HoseSockets.ContainsByPredicate([this](const auto& Socket)
		{ return !IsValid(Socket) || Socket->GetOwner() != this || !Socket->IsRegistered(); })) RebuildHoseSockets();
	UpdateControlLayout();
	Super::BeginPlay();
	if (bIsActive) SetActorTickEnabled(true);
}

void AManualColaFillingMachine::UpdateControlLayout()
{
	// Old blueprints can still carry the placeholder mesh on this inherited component.
	Pipe->SetStaticMesh(nullptr);
	ContentSelectorDial->SetVisibility(!bUseFlowController,true);
	if (ContentSelectorDial->ComponentHasTag(TEXT("ShopInteractionPoint")))
		ContentSelectorDial->SetCollisionEnabled(bUseFlowController ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	Lever->SetVisibility(!bUseFlowController);
	FlowInletCollar->SetVisibility(bUseFlowController);
	AutoSealSwitch->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	UpdateAutoSealLabel();
}

void AManualColaFillingMachine::UpdateAutoSealLabel()
{
	const bool bOn=Production && Production->bAutoApplySealOnRelease;
	if (DisplayedAutoSeal.IsSet() && DisplayedAutoSeal.GetValue()==bOn) return;
	DisplayedAutoSeal=bOn;
	AutoSealLabel->SetText(FText::FromString(bOn ? TEXT("AUTO SEAL ON") : TEXT("AUTO SEAL OFF")));
	AutoSealLabel->SetTextRenderColor(bOn ? FColor(131,239,207) : FColor(245,186,111));
}

void AManualColaFillingMachine::OnApplySharedDefinitions(const FPropProductDefinition* Product,const FPropContentDefinition* Content)
{
	Super::OnApplySharedDefinitions(Product,Content);
	if (HasAuthority() && GetNetMode()!=NM_Client && BoundMachineISID!=ISID) { Production->ResetEquipmentSession(); BoundMachineISID=ISID; }
}
void AManualColaFillingMachine::ResetTransientPoolState()
{
	if (Production) Production->ResetEquipmentSession();
	for (UHoseSocketComponent* Port : HoseSockets) if (Port)
	{
		Port->ClearManagedContentBinding();
		Port->RepairSocket();
		Port->SetFlowRequested(false);
	}
	CradleAlpha=0; DisplayedCapProgress=0; if (CradlePivot) CradlePivot->SetRelativeRotation(FRotator(RestCradlePitch,0,0));
	BoundMachineISID=0; Super::ResetTransientPoolState();
}

bool AManualColaFillingMachine::IsCradleInFillPosition() const
{
	return CradleAlpha>=1.f-UE_KINDA_SMALL_NUMBER;
}

void AManualColaFillingMachine::UpdateCradleMotion(float Dt)
{
	if (!Production || !CradlePivot || !BottleHolderPoint || !FMath::IsFinite(Dt) || Dt<=0) return;
	if (const auto* Layout = Cast<ASocketColaFillingMachine>(this); Layout && Layout->IsMachinePlacementActive()) return;
	auto* Manager=APropManager::Get(this);
	auto* Bottle=Manager ? Cast<AColaPropActor>(Manager->GetActiveActorFromISID(Production->View.SeatedBottleISID)) : nullptr;
	const bool OwnsBottle=IsValid(Bottle) && Bottle->bIsActive && Bottle->ISID==Production->View.SeatedBottleISID
		&& Bottle->IsSeatedInFiller() && !Bottle->bIsHeld && !Bottle->bIsInMotion
		&& (!Bottle->RocketComp || !Bottle->RocketComp->IsPoweredFlight());
	const bool bAuthority=HasAuthority() && GetNetMode()!=NM_Client;
	// Check before moving the holder, so external grabs/teleports are not pulled back into the machine.
	const bool WasAligned=bAuthority && OwnsBottle && Production->IsBottleAligned(Bottle->ISID);
	const FTransform PreviousHolderTransform=BottleHolderPoint->GetComponentTransform();
	const bool WasReady=IsCradleInFillPosition();
	if (bAuthority)
	{
		const float Target=Production->IsLeverHoldingBottle(Production->View.SeatedBottleISID) ? 1.f : 0.f;
		CradleAlpha=FMath::FInterpConstantTo(CradleAlpha,Target,Dt,1.f/FMath::Max(.05f,CradleTravelSeconds));
	}
	CradlePivot->SetRelativeRotation(FRotator(FMath::Lerp(RestCradlePitch,FillingCradlePitch,CradleAlpha),0,0));
	if (WasAligned && !PreviousHolderTransform.Equals(BottleHolderPoint->GetComponentTransform()))
		Manager->InitializeInstanceTransform(Bottle->ISID,FTransform(BottleHolderPoint->GetComponentQuat(),BottleHolderPoint->GetComponentLocation(),Bottle->GetActorScale3D()));
	else if (GetNetMode()==NM_Client && OwnsBottle)
	{
		// The inventory snapshot, active prop and cradle can arrive in different packets.
		const FTransform Pose(BottleHolderPoint->GetComponentQuat(),BottleHolderPoint->GetComponentLocation(),Bottle->GetActorScale3D());
		if (!Bottle->GetActorTransform().Equals(Pose,.05f))
			Bottle->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics);
	}
	if (!bAuthority && Manager) Production->RefreshClientSupplyPlacement(*Manager);
	UpdateLeverVisual(Dt);
	if (CappingHead)
	{
		const bool HasCap=Bottle && Bottle->LocalUniqueData.EventData.AttachedCapISID>0;
		CappingHead->SetVisibility(HasCap,true);
		DisplayedCapProgress=FMath::FInterpConstantTo(DisplayedCapProgress,HasCap ? Bottle->GetCapSealed() : 0.f,Dt,2.f);
		CappingHead->SetRelativeRotation(FRotator(0,DisplayedCapProgress*CapDisplayTurns,0));
	}
	if (bAuthority && !WasReady && Production->View.bLeverHeld && IsCradleInFillPosition()) Production->UpdateManualEquipment();
}

void AManualColaFillingMachine::UpdateLeverVisual(float DeltaSeconds)
{
	if (Lever) Lever->SetRelativeRotation(FRotator(0,0,FMath::Lerp(35.f,75.f,CradleAlpha)));
}

void AManualColaFillingMachine::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bIsActive || !Production) return;
	// Reactivating a pooled prop makes all of its child meshes visible.
	if (bUseFlowController && (ContentSelectorDial->IsVisible() || Lever->IsVisible())) UpdateControlLayout();
	UpdateAutoSealLabel();
	if (HasAuthority() && ISID<=0 && !PropDataAsset)
		if (auto* Registry=APropManager::Get(this); Registry && Registry->ShopSupplies)
			if (const auto* Catalog=Registry->ShopSupplies->Catalog.LoadSynchronous())
				if (const auto* Offer=Catalog->FindOffer(TEXT("ManualFillingMachine"))) PropDataAsset=Offer->Product.LoadSynchronous();
	if (HasAuthority() && GetNetMode()!=NM_Client && ISID<=0 && PropDataAsset)
	{
		if (auto* Manager=APropManager::Get(this); Manager && Manager->PropManagerReady && GetOwner()!=Manager)
		{
			// Register the placed stand as working equipment at its authored pose.
			const int32 ID=Manager->ConvertPlacedActorToISM(this,true);
			AActivePropActor* Equipment=nullptr;
			if (ID>0) Manager->PromoteInstanceToASM(ID,EPropActivationReason::LevelPlacement,false,Equipment);
			return;
		}
	}
	UpdateCradleMotion(Dt);
	// A delivery callback can stop flow while the hose transaction is still guarded.
	// Park that receiver on the next frame so replacing/removing the bottle keeps the hose connected.
	if (HasAuthority()) if (auto* Registry=APropManager::Get(this))
		for (UHoseSocketComponent* Port:HoseSockets)
			if (Port && Port->IsManagedReceivingProxy() && !Port->bRequestsFlow && Port->GetManagedContentISID()>0)
				Port->ConfigureManagedReceivingProxy(Registry,0,Port->ResourceID);
	for (int32 Side:{-1,1})
		if (auto* Jaw=Cast<UStaticMeshComponent>(GetDefaultSubobjectByName(Side<0 ? TEXT("NeckClampLeft") : TEXT("NeckClampRight"))))
		{
			FVector Location=Jaw->GetRelativeLocation();
			Location.X=FMath::FInterpTo(float(Location.X),Side*(Production->View.bLeverHeld ? 6.f : 14.f),Dt,12.f);
			Jaw->SetRelativeLocation(Location);
		}
	if (ContentSelectorDial && !bUseFlowController) ContentSelectorDial->SetRelativeRotation(ContentDialRestRotation
		* FQuat(FVector::UpVector, 2.f * PI * Production->View.SelectedContentPort / Production->GetSupportedContentCount()));
	auto* Manager=APropManager::Get(this);
	for (int32 I=0; I<SupplyPipes.Num(); ++I)
	{
		const int32 SourceID=Production->View.SupplyPorts.IsValidIndex(I) ? Production->View.SupplyPorts[I].SourceISID : 0;
		const auto* Source=Manager ? Manager->FindISIDData(SourceID) : nullptr;
		const bool Connected=Source && FVector::DistSquared(GetActorLocation(),Manager->GetTransform(SourceID).GetLocation())<=FMath::Square(BottleFiller->MaxConnectionDistance);
		auto* Line=SupplyPipes[I].Get(); Line->SetVisibility(Connected);
		if (!Connected) continue;
		const FVector Start=GetHosePortTransform(I).GetLocation();
		const FVector End=Manager->GetTransform(SourceID).TransformPosition(FVector(0,0,40));
		const FVector Span=End-Start;
		Line->SetWorldLocationAndRotation((Start+End)*.5,UKismetMathLibrary::MakeRotFromZ(Span));
		Line->SetWorldScale3D(FVector(.025,.025,FMath::Max(.01,Span.Size()/100.)));
	}
}
