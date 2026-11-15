#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/BottleFillingComponent.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/KismetMathLibrary.h"

AManualColaFillingMachine::AManualColaFillingMachine()
{
	bAutoConvertToISMIfAtRest=false; State=EPropState::Unsettled;
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
	Visual(TEXT("NozzleSupport"),FVector(40,0,140),FVector(.05,.05,2.4),Cube.Object);
	Visual(TEXT("NozzleArm"),FVector(20,0,245),FVector(.45,.08,.08),Cube.Object);
	Visual(TEXT("FillingNozzle"),FVector(0,0,237),FVector(.06,.06,.18),Cylinder.Object);
	Visual(TEXT("BottleHolderRing"),FVector(0,0,53),FVector(.78,.78,.04),Cylinder.Object);
	Visual(TEXT("TemporarySealButton"),FVector(-35,-40,58),FVector(.12,.12,.08),Cylinder.Object);
	Lever=Visual(TEXT("ManualFillingLever"),FVector(45,-35,125),FVector(.04,.04,.55),Cylinder.Object);
	Lever->SetRelativeRotation(FRotator(0,0,35));
	Pipe=Visual(TEXT("SingleLiquidSupplyPipe"),FVector::ZeroVector,FVector(.025,.025,1),Cylinder.Object);
	Pipe->SetVisibility(false);
	Production=CreateDefaultSubobject<UShopProductionStationComponent>(TEXT("ManualProduction"));
	BottleFiller=CreateDefaultSubobject<UBottleFillingComponent>(TEXT("ManualBottleFiller"));
	BottleFiller->PortionsPerSecond=1; BottleFiller->MaxConnectionDistance=400; BottleFiller->TargetContentLevel=1;
	Production->Filler=BottleFiller; Production->bManualLeverMode=true; Production->bAllowSoloCap=true;
	Production->bRequiresCooperativeCap=false;
	auto Box=[&](const TCHAR* Name,const TCHAR* Tag,FVector Position,FVector Extent)
	{
		auto* Part=CreateDefaultSubobject<UBoxComponent>(Name); Part->SetupAttachment(PropMesh);
		Part->ComponentTags.Add(Tag); Part->SetRelativeLocation(Position); Part->SetBoxExtent(Extent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); return Part;
	};
	Production->BottleInput=Box(TEXT("BottleInput"),TEXT("ShopBottleInput"),FVector(0,0,145),FVector(45,45,110));
	Production->CylinderInput=Box(TEXT("SupplyPort"),TEXT("ShopCylinderInput"),FVector(100,0,50),FVector(25,25,45));
	Production->CapInput=Box(TEXT("CapInput"),TEXT("ShopCapInput"),FVector(35,-25,70),FVector(40,40,35));
	auto Point=[&](const TCHAR* Name,const TCHAR* Tag,FVector Position)
	{
		auto* Part=CreateDefaultSubobject<USceneComponent>(Name); Part->SetupAttachment(PropMesh);
		Part->ComponentTags.Add(Tag); Part->SetRelativeLocation(Position); return Part;
	};
	Production->BottleHolder=Point(TEXT("BottleHolder"),TEXT("ManualBottleHolder"),FVector(0,0,51));
	Production->SupplyOutput=Point(TEXT("SupplyOutput"),TEXT("ShopSupplyOutput"),FVector(-500,0,60));
	Point(TEXT("ManualControls"),TEXT("ShopInteractionPoint"),FVector(0,-100,45));
}

void AManualColaFillingMachine::BeginPlay()
{
	Super::BeginPlay();
	if (bIsActive) SetActorTickEnabled(true);
}

void AManualColaFillingMachine::OnApplySharedDefinitions(const FPropProductDefinition* Product,const FPropContentDefinition* Content)
{
	Super::OnApplySharedDefinitions(Product,Content);
	if (HasAuthority() && GetNetMode()!=NM_Client && BoundMachineISID!=ISID) { Production->ResetEquipmentSession(); BoundMachineISID=ISID; }
}
void AManualColaFillingMachine::ResetTransientPoolState()
{
	if (Production) Production->ResetEquipmentSession();
	BoundMachineISID=0; Super::ResetTransientPoolState();
}
void AManualColaFillingMachine::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bIsActive || !Production || !Pipe) return;
	if (HasAuthority() && GetNetMode()!=NM_Client && ISID<=0 && PropDataAsset)
	{
		if (auto* Manager=APropManager::Get(this); Manager && Manager->PropManagerReady && GetOwner()!=Manager)
		{
			// Placed non-resting props are registered as pool actors, but do not
			// receive a logical inventory ID until converted. Reuse that existing
			// conversion once, then promote the same identity as working equipment.
			const int32 ID=Manager->ConvertPlacedActorToISM(this,true);
			AActivePropActor* Equipment=nullptr;
			if (ID>0) Manager->PromoteInstanceToASM(ID,EPropActivationReason::Interaction,false,Equipment);
			return;
		}
	}
	if (Lever) Lever->SetRelativeRotation(FRotator(0,0,BottleFiller->bIsFilling ? 75.f : 35.f));
	auto* Manager=APropManager::Get(this);
	const auto* Source=Manager ? Manager->FindISIDData(Production->View.ConnectedSourceISID) : nullptr;
	const FVector Start=GetActorTransform().TransformPosition(FVector(45,0,70));
	const FVector End=Source ? Manager->GetTransform(Source->ISID).TransformPosition(FVector(0,0,40)) : Start;
	const FVector Span=End-Start;
	const bool Connected=Source && Span.SizeSquared()<=FMath::Square(BottleFiller->MaxConnectionDistance+100);
	Pipe->SetVisibility(Connected);
	if (Connected)
	{
		Pipe->SetWorldLocationAndRotation((Start+End)*.5,UKismetMathLibrary::MakeRotFromZ(Span));
		Pipe->SetWorldScale3D(FVector(.025,.025,FMath::Max(.01,Span.Size()/100.)));
	}
}
