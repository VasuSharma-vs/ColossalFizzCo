#include "Shop/SocketColaFillingMachine.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CollisionCableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Shop/BottleFillingComponent.h"
#include "Shop/ShopProductionStationComponent.h"

ASocketColaFillingMachine::ASocketColaFillingMachine()
{
	FillingController = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SM_FillingController"));
	FillingController->SetupAttachment(PropMesh);
	FillingController->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	FillingController->BodyInstance.bAutoWeld = false;
	FillingController->ComponentTags.Add(TEXT("ShopInteractionPoint"));

	Lever->SetupAttachment(FillingController);
	Lever->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Lever->SetCollisionResponseToAllChannels(ECR_Ignore);
	Lever->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Lever->SetRelativeRotation(FRotator(0, 0, -10));

	StandRotator = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SM_Bottle_StandRotater"));
	StandRotator->SetupAttachment(CradlePivot);
	StandRotator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RestCradlePitch = 0.f;
	FillingCradlePitch = -20.f;

	CapPin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SM_Cap_Pin"));
	CapPin->SetupAttachment(CradlePlatform);
	CapPin->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BottleSocket = CreateDefaultSubobject<UArrowComponent>(TEXT("BottleSocket"));
	BottleSocket->SetupAttachment(CradlePlatform);
	BottleSocket->SetHiddenInGame(true);
	BottleHolderPoint->SetupAttachment(BottleSocket);
	BottleHolderPoint->SetRelativeTransform(FTransform::Identity);

	CapSocket = CreateDefaultSubobject<UArrowComponent>(TEXT("CapSocket"));
	CapSocket->SetupAttachment(CradlePlatform);
	CapSocket->SetHiddenInGame(true);

	BottleInputVolume->SetupAttachment(BottleSocket);
	BottleInputVolume->SetRelativeTransform(FTransform::Identity);
	BottleInputVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BottleInputVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BottleInputVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BottleInputVolume->ComponentTags.Add(TEXT("ShopInteractionPoint"));

	CapInputVolume->SetupAttachment(CapSocket);
	CapInputVolume->SetRelativeTransform(FTransform::Identity);
	CapInputVolume->ComponentTags.Add(TEXT("ShopInteractionPoint"));
	SealInputVolume->ComponentTags.Add(TEXT("ShopInteractionPoint"));

	SupplyInputVolume->SetupAttachment(FillingController);
	HoseConnections->SetRelativeTransform(FTransform::Identity);
	for (int32 Index=0; Index<HosePortTransforms.Num(); ++Index)
		HosePortTransforms[Index] = GetDefaultHosePortTransform(Index);

	ContentSelectorDial->SetupAttachment(FillingController);
	ContentSelectorDial->SetRelativeLocation(FVector(25, -51, 105));
	ContentSelectorDial->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContentSelectorDial->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContentSelectorDial->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ContentSelectorDial->SetGenerateOverlapEvents(false);
	ContentSelectorDial->ComponentTags.Add(TEXT("ShopInteractionPoint"));
	ControlCable = CreateDefaultSubobject<UCollisionCableComponent>(TEXT("ControllerCable"));
	ControlCable->SetupAttachment(CapPin);
	ControlCable->bEnableGrabbing = false;
	ControlCable->bEnableNetworkSync = false;
	ControlCable->bAttachStart = true;
	ControlCable->bAttachEnd = true;
	ControlCable->AttachEndTo.ComponentProperty = GET_MEMBER_NAME_CHECKED(ASocketColaFillingMachine, FillingController);
	OnPropBecomeActive.AddDynamic(this, &ASocketColaFillingMachine::ResetControlCable);

	// These primitive meshes belong to the older bench layout.
	const FName ReplacedParts[] = {
		TEXT("NozzleSupport"), TEXT("NozzleArm"), TEXT("FillingNozzle"), TEXT("CappingHead"),
		TEXT("CapTurnIndicator"), TEXT("NeckClampLeft"), TEXT("NeckClampRight"),
		TEXT("BottleHolderRing"), TEXT("CapTrayPlatform")
	};
	for (FName Name : ReplacedParts)
	{
		if (auto* Part = Cast<UStaticMeshComponent>(GetDefaultSubobjectByName(Name)))
		{
			Part->SetStaticMesh(nullptr);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

FVector ASocketColaFillingMachine::GetSupplyConnectionOrigin() const
{
	return FillingController->GetComponentLocation();
}

FTransform ASocketColaFillingMachine::GetDefaultHosePortTransform(int32 Index) const
{
	const double X = ((Index % 4) - 1.5) * 30;
	return FTransform(FRotator(0, -90, 0), FVector(X, 0, (Index / 4) * 30));
}

void ASocketColaFillingMachine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASocketColaFillingMachine, bWorldLeverPressed);
	DOREPLIFETIME(ASocketColaFillingMachine, InputPlacements);
}

void ASocketColaFillingMachine::UpdateLeverVisual(float DeltaSeconds)
{
	const bool bPulled = bWorldLeverPressed || Production->View.bLeverHeld;
	FRotator Rotation = Lever->GetRelativeRotation();
	const float Speed = 110.f / FMath::Max(.05f, LeverTravelSeconds);
	Rotation.Roll = FMath::FInterpConstantTo(float(Rotation.Roll), bPulled ? 100.f : -10.f, DeltaSeconds, Speed);
	Lever->SetRelativeRotation(Rotation);
}

void ASocketColaFillingMachine::Tick(float DeltaSeconds)
{
	if (HasAuthority() && bWorldLeverPressed)
	{
		APawn* Operator = WorldLeverOperator.Get();
		if (!bIsActive || bIsHeld || !CanReachControl(Operator, Lever))
			ReleaseWorldLever();
		else if (Production->View.bLeverHeld && Production->View.LeverOperator == Operator)
			BottleFiller->LeverExpiresAt = GetWorld()->GetTimeSeconds() + FMath::Max(.15f, Production->LeverLeaseSeconds);
	}
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_Client) UpdateClientInputPlacements(DeltaSeconds);
	else if (HasAuthority()) UpdateInputPlacements(DeltaSeconds);
}

void ASocketColaFillingMachine::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelInputPlacements();
	Super::EndPlay(Reason);
}

void ASocketColaFillingMachine::ResetTransientPoolState()
{
	CancelInputPlacements();
	ReleaseWorldLever();
	Super::ResetTransientPoolState();
	Lever->SetRelativeRotation(FRotator(0, 0, -10));
}

void ASocketColaFillingMachine::ResetControlCable(AActivePropActor*, int32, int32, FTransform)
{
	ControlCable->SetAttachEndToComponent(FillingController);
	ControlCable->ResetCable();
}
