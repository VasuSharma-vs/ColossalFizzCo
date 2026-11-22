#include "Shop/ColaTableCustomerComponent.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CapsuleComponent.h"
#include "Components/InteractableInstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropManager.h"
#include "Props/PropProductCatalog.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float ApproachAcceptance = 36.f;
	constexpr float ApproachClearance = 45.f;
	constexpr float ScanInterval = .5f;
	constexpr double LiftSeconds = .60;
	constexpr double DrinkSeconds = .55;
	constexpr double HeadTiltSeconds = .22;
	constexpr double LowerSeconds = .65;
	constexpr double LowerStartsAt = LiftSeconds + DrinkSeconds;
	constexpr double SipDuration = LowerStartsAt + LowerSeconds;
	constexpr double ConsumeAfter = LiftSeconds + DrinkSeconds * .5;
	constexpr float TableContactTolerance = 18.f;
	constexpr double ThrowStartLead = .12;
	constexpr double ThrowFlightTime = 1.25;
	constexpr double ClientReleaseSyncWindow = .25;
	const FName RightHandBone(TEXT("hand_r"));

	void GetThrowFlightIntegrals(double Time, double Damping, double& OutVelocity, double& OutGravity)
	{
		OutVelocity = Time;
		OutGravity = .5 * FMath::Square(Time);
		if (Damping > UE_KINDA_SMALL_NUMBER)
		{
			OutVelocity = (1.0 - FMath::Exp(-Damping * Time)) / Damping;
			OutGravity = (Time - OutVelocity) / Damping;
		}
	}

	bool IsCarryingBottle(EColaTableCustomerState State)
	{
		return State == EColaTableCustomerState::Drinking
			|| State == EColaTableCustomerState::ApproachingBin
			|| State == EColaTableCustomerState::PreparingThrow;
	}

	FVector HeadLocation(const ACharacter& Character)
	{
		if (const auto* Mesh = Character.GetMesh(); Mesh && Mesh->DoesSocketExist(TEXT("head")))
			return Mesh->GetSocketLocation(TEXT("head"));
		return Character.GetActorLocation() + FVector(0, 0, Character.GetSimpleCollisionHalfHeight() * .8);
	}

	bool HasSightOfBottle(const ACharacter& Character, const FISIDRuntimeIndex& Bottle, FVector Target)
	{
		FHitResult Hit;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaTableSight), false, &Character);
		if (!Character.GetWorld()->LineTraceSingleByChannel(Hit, HeadLocation(Character), Target, ECC_Visibility, Query)) return true;
		if (Bottle.ActiveActor && Hit.GetActor() == Bottle.ActiveActor.Get()) return true;
		const auto* Instances = Cast<UInteractableISMComponent>(Hit.GetComponent());
		return Instances && Instances->FindISID(Hit.Item) == Bottle.ISID;
	}
}

AColaBottleBin::AColaBottleBin()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("BinRoot")));
	BinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BinMesh"));
	BinMesh->SetupAttachment(GetRootComponent());
	BinMesh->SetRelativeLocation(FVector(0, -42.5, 0));
	BinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BinMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Bucket(TEXT("/Game/Assest/meshs/SM_Bucket.SM_Bucket"));
	if (Bucket.Succeeded()) BinMesh->SetStaticMesh(Bucket.Object);

	Opening = CreateDefaultSubobject<USceneComponent>(TEXT("Opening"));
	Opening->SetupAttachment(GetRootComponent());
	Opening->SetRelativeLocation(FVector(0, 0, 49));
}

FVector AColaBottleBin::GetOpeningLocation() const
{
	return Opening->GetComponentLocation();
}

bool AColaBottleBin::CrossedOpening(FVector Previous, FVector Current) const
{
	const FTransform Rim = Opening->GetComponentTransform();
	const FVector From = Rim.InverseTransformPosition(Previous);
	const FVector To = Rim.InverseTransformPosition(Current);
	if (From.Z <= 0 || To.Z > 0) return false;
	const FVector Crossing = FMath::Lerp(From, To, From.Z / (From.Z - To.Z));
	return Crossing.SizeSquared2D() <= FMath::Square(OpeningRadius);
}

UColaTableCustomerComponent::UColaTableCustomerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
	SipAnimationLayer = TSoftClassPtr<UAnimInstance>(
		FSoftObjectPath(TEXT("/Game/AI/BasicNPC/ABP_ColaCustomerSip.ABP_ColaCustomerSip_C")));

	static ConstructorHelpers::FObjectFinder<UPropContentDataAsset> Lemonade(
		TEXT("/Game/Props/Products/Content/DA_Content_Lemonade.DA_Content_Lemonade"));
	RequestedCola = Lemonade.Object;
}

void UColaTableCustomerComponent::BeginPlay()
{
	Super::BeginPlay();
	Customer = Cast<ACharacter>(GetOwner());
	if (CreationMethod == EComponentCreationMethod::Instance && GetOwner()->IsNetStartupActor()) SetNetAddressable();
	if (!Customer.IsValid() || !IsValid(ServingTable) || !IsValid(BottleBin))
	{
		SetComponentTickEnabled(false);
		return;
	}

	TInlineComponentArray<UStaticMeshComponent*> Meshes(ServingTable);
	for (UStaticMeshComponent* Mesh : Meshes)
		if (Mesh->GetFName() == TabletopMeshName && Mesh->GetStaticMesh()) { Tabletop = Mesh; break; }
	if (!Tabletop.IsValid() || !Customer->GetMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s needs a skeletal mesh and a configured tabletop mesh."), *GetPathName());
		SetComponentTickEnabled(false);
		return;
	}

	USkeletalMeshComponent* Mesh = Customer->GetMesh();
	if (UClass* Layer = SipAnimationLayer.LoadSynchronous())
	{
		Mesh->SetOverridePostProcessAnimBP(Layer);
	}
	if (Mesh->DoesSocketExist(BottleGripSocket)) GripSocket = BottleGripSocket;
	else if (Mesh->DoesSocketExist(RightHandBone)) GripSocket = RightHandBone;
	AddTickPrerequisiteComponent(Mesh);
	CreateRequestLabel();
	OnRep_VisitState();
	if (!GetOwner()->HasAuthority()) return;

	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	UCharacterMovementComponent* Movement = Customer->GetCharacterMovement();
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = false;
	Movement->bRequestedMoveUseAcceleration = false;
}

void UColaTableCustomerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearClientBottleGrip();
	if (IsValid(RequestLabel)) RequestLabel->DestroyComponent();
	if (EndPlayReason == EEndPlayReason::Destroyed && Customer.IsValid() && GetOwner()->HasAuthority() && BottleISID > 0)
	{
		if (auto* Manager = APropManager::Get(this); Manager && VisitState != EColaTableCustomerState::ThrowingBottle)
			Manager->ReleaseGrab(BottleISID, Customer.Get());
		if (auto* AI = Controller.Get()) AI->StopMovement();
	}
	SetBottleCarryMovement(false);
	Super::EndPlay(EndPlayReason);
}

void UColaTableCustomerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UColaTableCustomerComponent, VisitState);
	DOREPLIFETIME(UColaTableCustomerComponent, BottleISID);
	DOREPLIFETIME(UColaTableCustomerComponent, SipStartedAt);
	DOREPLIFETIME(UColaTableCustomerComponent, ThrowStartedAt);
	DOREPLIFETIME(UColaTableCustomerComponent, BottleReleasePose);
	DOREPLIFETIME(UColaTableCustomerComponent, BottleReleaseVelocity);
	DOREPLIFETIME(UColaTableCustomerComponent, BottleMouthInHand);
	DOREPLIFETIME(UColaTableCustomerComponent, BottleGripInSocket);
}

double UColaTableCustomerComponent::GetAnimationElapsed(double StartedAt) const
{
	const UWorld* World = GetWorld();
	if (StartedAt < 0.0 || !World) return -1.0;

	const auto* GameState = World->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	return Now - StartedAt;
}

void UColaTableCustomerComponent::GetSipPoseWeights(float& OutLift, float& OutHeadTilt) const
{
	const double Elapsed = VisitState == EColaTableCustomerState::Drinking ? GetAnimationElapsed(SipStartedAt) : -1.0;
	const double LowerWeight = 1.0 - FMath::SmoothStep(LowerStartsAt, SipDuration, Elapsed);
	OutLift = FMath::SmoothStep(0.0, LiftSeconds, Elapsed) * LowerWeight;
	OutHeadTilt = FMath::SmoothStep(LiftSeconds, LiftSeconds + HeadTiltSeconds, Elapsed) * LowerWeight;
}

double UColaTableCustomerComponent::GetThrowElapsed() const
{
	const double Elapsed = GetAnimationElapsed(ThrowStartedAt);
	if (VisitState == EColaTableCustomerState::PreparingThrow) return FMath::Min(Elapsed, ColaCustomerThrow::ReleaseAt);
	return Elapsed < ColaCustomerThrow::RecoveryEnd ? Elapsed : -1.0;
}

void UColaTableCustomerComponent::CreateRequestLabel()
{
	if (GetNetMode() == NM_DedicatedServer || !FSlateApplication::IsInitialized()
		|| !RequestedCola || RequestedCola->Properties.DisplayName.IsEmpty()) return;

	static const FSlateRoundedBoxBrush Background(FLinearColor(.025f, .065f, .08f, .96f), 8.f);
	RequestLabel = NewObject<UWidgetComponent>(GetOwner(), TEXT("ColaRequestLabel"), RF_Transient);
	RequestLabel->SetWidgetSpace(EWidgetSpace::Screen);
	RequestLabel->SetDrawAtDesiredSize(true);
	RequestLabel->SetPivot(FVector2D(.5, 1));
	RequestLabel->SetWindowFocusable(false);
	RequestLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RequestLabel->SetCanEverAffectNavigation(false);
	RequestLabel->SetSlateWidget(
		SNew(SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(&Background)
		.Padding(FMargin(12, 6, 12, 7))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ColaTableCustomer", "Wants", "WANTS"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FLinearColor(.94f, .70f, .36f))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(RequestedCola->Properties.DisplayName)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FLinearColor(.91f, .93f, .87f))
			]
		]);

	USkeletalMeshComponent* Mesh = Customer->GetMesh();
	const FName HeadSocket = TEXT("head");
	if (Mesh->DoesSocketExist(HeadSocket)) RequestLabel->SetupAttachment(Mesh, HeadSocket);
	else RequestLabel->SetupAttachment(Customer->GetRootComponent());
	RequestLabel->RegisterComponent();

	FVector Position = HeadLocation(*Customer);
	const double Radius = Customer->GetSimpleCollisionRadius();
	const double CapsuleTop = Customer->GetActorLocation().Z + Customer->GetSimpleCollisionHalfHeight();
	Position.Z = FMath::Max(Position.Z + .55 * Radius, CapsuleTop) + .14 * Radius;
	RequestLabel->SetWorldLocation(Position);
}

void UColaTableCustomerComponent::OnRep_VisitState()
{
	const bool bRequesting = VisitState == EColaTableCustomerState::ApproachingTable
		|| VisitState == EColaTableCustomerState::WaitingAtTable;
	if (IsValid(RequestLabel)) RequestLabel->SetVisibility(bRequesting);
	if (GetNetMode() == NM_Client)
	{
		const bool bCarrying = IsCarryingBottle(VisitState);
		SetComponentTickEnabled(bCarrying || VisitState == EColaTableCustomerState::ThrowingBottle);
		if (!bCarrying) ClearClientBottleGrip();
	}
}

void UColaTableCustomerComponent::UpdateClientBottleGrip()
{
	auto* Manager = APropManager::Get(this);
	auto* Bottle = Manager ? Manager->GetActiveActorFromISID(BottleISID) : nullptr;
	if (!IsValid(Bottle) || !Bottle->IsCarriedForDrinking()
		|| (Bottle->InteractionOwner && Bottle->InteractionOwner != GetOwner()))
	{
		ClearClientBottleGrip();
		return;
	}
	if (ClientGrippedBottle.Get() != Bottle) ClearClientBottleGrip();

	// The active bottle can arrive after the customer state or change class when it empties.
	USkeletalMeshComponent* Mesh = Customer->GetMesh();
	USceneComponent* Root = Bottle->GetRootComponent();
	if (!Root || !Mesh) return;
	if (Root->GetAttachParent() != Mesh || Root->GetAttachSocketName() != GripSocket)
	{
		if (Bottle->PropMesh && Bottle->PropMesh->IsSimulatingPhysics()) Bottle->PropMesh->SetSimulatePhysics(false);
		if (!Bottle->AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, GripSocket)) return;
	}
	if (!Root->GetRelativeTransform().Equals(BottleGripInSocket)) Root->SetRelativeTransform(BottleGripInSocket);
	ClientGrippedBottle = Bottle;
}

void UColaTableCustomerComponent::ClearClientBottleGrip()
{
	if (auto* Bottle = ClientGrippedBottle.Get(); Bottle && Bottle->GetAttachParentActor() == GetOwner())
		Bottle->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ClientGrippedBottle.Reset();
}

void UColaTableCustomerComponent::SyncClientBottleRelease()
{
	if (ThrowStartedAt < 0.0) return;
	const double Age = FMath::Max(0.0, GetAnimationElapsed(ThrowStartedAt) - ColaCustomerThrow::ReleaseAt);
	if (Age > ClientReleaseSyncWindow)
	{
		SetComponentTickEnabled(false);
		return;
	}

	const auto* Manager = APropManager::Get(this);
	auto* Bottle = Manager ? Manager->GetActiveActorFromISID(BottleISID) : nullptr;
	if (!IsValid(Bottle) || Bottle->bIsHeld || Bottle->IsCarriedForDrinking()) return;
	auto* Body = Bottle->PropMesh.Get();
	if (!Body || !Body->IsSimulatingPhysics()) return;
	SetComponentTickEnabled(false);

	// Activation can briefly restore a pooled bottle's old position on a client.
	const FVector Gravity(0, 0, GetWorld()->GetGravityZ());
	const double Damping = Body->GetLinearDamping();
	double VelocityIntegral, GravityIntegral;
	GetThrowFlightIntegrals(Age, Damping, VelocityIntegral, GravityIntegral);
	const FVector Position = BottleReleasePose.GetLocation()
		+ BottleReleaseVelocity * VelocityIntegral + Gravity * GravityIntegral;
	if (Bottle->GetActorLocation().Equals(Position, 25.f)) return;

	Body->SetWorldLocationAndRotation(Position, BottleReleasePose.GetRotation(), false, nullptr,
		ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(BottleReleaseVelocity * FMath::Exp(-Damping * Age) + Gravity * VelocityIntegral);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void UColaTableCustomerComponent::ChangeState(EColaTableCustomerState NextState, double Now)
{
	VisitState = NextState;
	SipStartedAt = -1.0;
	if (NextState != EColaTableCustomerState::PreparingThrow && NextState != EColaTableCustomerState::ThrowingBottle)
		ThrowStartedAt = -1.0;
	NextActionTime = Now;
	bHasApproachTarget = false;
	SetBottleCarryMovement(IsCarryingBottle(NextState));
	OnRep_VisitState();
	GetOwner()->ForceNetUpdate();
}

void UColaTableCustomerComponent::SetBottleCarryMovement(bool bCarrying)
{
	if (!Customer.IsValid()) return;
	auto* Movement = Customer->GetCharacterMovement();
	if (bCarrying && !WalkSpeedBeforeBottle.IsSet())
	{
		WalkSpeedBeforeBottle = Movement->MaxWalkSpeed;
		Movement->MaxWalkSpeed = FMath::Min(*WalkSpeedBeforeBottle, FMath::Max(1.f, BottleCarryWalkSpeed));
	}
	else if (!bCarrying && WalkSpeedBeforeBottle.IsSet())
	{
		Movement->MaxWalkSpeed = *WalkSpeedBeforeBottle;
		WalkSpeedBeforeBottle.Reset();
	}
}

void UColaTableCustomerComponent::Face(FVector Target) const
{
	const FVector Direction = Target - Customer->GetActorLocation();
	if (!Direction.IsNearlyZero()) Customer->SetActorRotation(FRotator(0, Direction.Rotation().Yaw, 0));
}

bool UColaTableCustomerComponent::Approach(AAIController& AI, const FBox& Bounds, double Now, const FVector* BottleLocation)
{
	const float Acceptance = BottleLocation ? 16.f : ApproachAcceptance;
	if (bHasApproachTarget && FVector::DistSquared2D(ApproachTarget, Customer->GetActorLocation()) <= FMath::Square(Acceptance + 4.f))
	{
		AI.StopMovement();
		Customer->GetCharacterMovement()->StopMovementImmediately();
		return true;
	}
	if (Now < NextActionTime || AI.GetMoveStatus() != EPathFollowingStatus::Idle) return false;
	NextActionTime = Now + ScanInterval;
	auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!Nav) return false;

	const FVector Center = Bounds.GetCenter();
	const FVector Reference = BottleLocation ? *BottleLocation : Center;
	const float Z = Customer->GetNavAgentLocation().Z;
	const float Offset = Customer->GetSimpleCollisionRadius() + ApproachClearance;
	const float X = FMath::Clamp(Reference.X, Bounds.Min.X, Bounds.Max.X);
	const float Y = FMath::Clamp(Reference.Y, Bounds.Min.Y, Bounds.Max.Y);
	TArray<FVector, TInlineAllocator<4>> Goals = {
		FVector(Bounds.Min.X - Offset, Y, Z), FVector(Bounds.Max.X + Offset, Y, Z),
		FVector(X, Bounds.Min.Y - Offset, Z), FVector(X, Bounds.Max.Y + Offset, Z)
	};
	const FVector CustomerLocation = Customer->GetActorLocation();
	const auto Score = [&](const FVector& Goal)
	{
		const double Travel = FVector::Dist2D(Goal, CustomerLocation);
		return BottleLocation ? FVector::Dist2D(Goal, *BottleLocation) + .2 * Travel : Travel;
	};
	Goals.Sort([&](const FVector& A, const FVector& B) { return Score(A) < Score(B); });
	for (const FVector& Goal : Goals)
	{
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Goal, Projected, FVector(100, 100, 180), &Customer->GetNavAgentPropertiesRef())) continue;
		if (FVector::DistSquared2D(Projected.Location, Goal) > FMath::Square(ApproachClearance)) continue;
		const auto Result = AI.MoveToLocation(Projected.Location, Acceptance, false, true, false, false, nullptr, false);
		if (Result == EPathFollowingRequestResult::Failed) continue;
		ApproachTarget = Projected.Location;
		bHasApproachTarget = true;
		return Result == EPathFollowingRequestResult::AlreadyAtGoal;
	}
	return false;
}

void UColaTableCustomerComponent::WalkAround(AAIController& AI, double Now)
{
	if (AI.GetMoveStatus() != EPathFollowingStatus::Idle || Now < NextActionTime) return;
	NextActionTime = Now + ScanInterval;
	auto* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!Nav) return;

	const FVector Start = Customer->GetNavAgentLocation();
	auto* NavData = Nav->GetNavDataForProps(Customer->GetNavAgentPropertiesRef());
	for (int32 Attempt = 0; Attempt < 5; ++Attempt)
	{
		FNavLocation Goal;
		if (!Nav->GetRandomReachablePointInRadius(Start, WanderRadius, Goal, NavData)) return;
		if (FVector::DistSquared2D(Start, Goal.Location) < FMath::Square(Customer->GetSimpleCollisionRadius() + 90.f)) continue;
		if (FVector::DistSquared2D(Goal.Location, Tabletop->Bounds.Origin) > FMath::Square(2.f * WanderRadius)) continue;
		if (AI.MoveToLocation(Goal.Location, ApproachAcceptance, false, true, false, false, nullptr, false)
			== EPathFollowingRequestResult::RequestSuccessful) return;
	}
}

int32 UColaTableCustomerComponent::FindBottle(const APropManager& Manager, FShopProductScanData& OutQuote) const
{
	if (!RequestedCola || RequestedCola->Properties.ContentID <= 0) return 0;
	const int32 RequestedID = Manager.CanonicalProductPID(RequestedCola->Properties.ContentID);
	const FTransform SurfaceTransform = Tabletop->GetComponentTransform();
	const FBox Surface = Tabletop->GetStaticMesh()->GetBoundingBox();
	const double ZTolerance = TableContactTolerance / FMath::Max(.01, FMath::Abs(SurfaceTransform.GetScale3D().Z));
	int32 BestID = 0;
	double Nearest = TNumericLimits<double>::Max();
	for (const auto& Pair : Manager.ISIDData)
	{
		const int32 ID = Pair.Key;
		const FISIDRuntimeIndex& Record = Pair.Value;
		if (ID <= 0 || Record.State == EPropState::Held || Record.State == EPropState::InMotion
			|| Record.RuntimeData.bInteractionLock || !Record.PackedRuntimeData.bIsGrabbable) continue;
		const auto* Contents = Manager.FindUniqueData(ID);
		const auto* Product = Manager.FindProductDefinition(Record.ProductID);
		if (!Contents || Contents->EventData.ContentLevel <= UE_SMALL_NUMBER || !Product || !Product->StaticMesh
			|| Manager.CanonicalProductPID(Contents->EventData.ContentID) != RequestedID
			|| !Product->InteractionProfile.bCanConsume || Product->ConsumableProfile.ConsumableType != EConsumablePropType::Drink
			|| Product->bIsBottleCap || Product->bLiquidVisualOnly || Manager.ResolveRootISID(ID) != ID) continue;
		const auto* Active = Record.ActiveActor.Get();
		if (Active && (Active->IsSeatedInFiller() || Active->GetVelocity().SizeSquared() > FMath::Square(65.f))) continue;

		const FTransform BottleTransform = Active ? Active->GetActorTransform() : Manager.GetTransform(ID);
		const FBox MeshBounds = Product->StaticMesh->GetBoundingBox();
		const FBox BottleOnTable = MeshBounds.TransformBy(BottleTransform.ToMatrixWithScale() * SurfaceTransform.ToInverseMatrixWithScale());
		const FVector Center = BottleOnTable.GetCenter();
		if (Center.X < Surface.Min.X || Center.X > Surface.Max.X || Center.Y < Surface.Min.Y || Center.Y > Surface.Max.Y
			|| FMath::Abs(BottleOnTable.Min.Z - Surface.Max.Z) > ZTolerance) continue;

		const FVector SightTarget = BottleTransform.TransformPosition(MeshBounds.GetCenter());
		const double Distance = FVector::DistSquared(Customer->GetActorLocation(), SightTarget);
		if (Distance >= Nearest || !HasSightOfBottle(*Customer, Record, SightTarget)) continue;
		FShopStockEntry Stock;
		if (Manager.ShopCheckout && (Manager.ShopCheckout->IsReserved(ID) || Manager.ShopCheckout->GetStockEntry(ID, Stock))) continue;
		FShopProductScanData Quote;
		if (!Manager.GetProductScanData(ID, Quote) || Quote.Price.bExpired || Quote.Price.SaleCents <= 0) continue;
		BestID = ID;
		Nearest = Distance;
		OutQuote = MoveTemp(Quote);
	}
	return BestID;
}

bool UColaTableCustomerComponent::BuyBottle(APropManager& Manager, const FShopProductScanData& Quote)
{
	if (!Manager.SharedWallet) return false;
	const int32 ID = Quote.ISID;
	const auto Result = Manager.SharedWallet->CommitWithResources(FGuid::NewGuid(), Quote.Price.SaleCents, TEXT("TableColaSale"), [&]
	{
		auto* Bottle = Manager.ReserveGrabForTravel(ID, Customer.Get());
		if (!Bottle) return false;
		bool bReady = Bottle->OnPlayerGrab(Customer->GetMesh(), GripSocket);
		if (bReady) Bottle->SetDrinkingCarry(true);
		if (bReady && Bottle->GetCapSealed() > 0.f)
		{
			if (auto* Cola = Cast<AColaPropActor>(Bottle)) Cola->RemoveCap(0.f, ECapRemovalOperation::DropQuietly);
			bReady = Bottle->GetCapSealed() <= 0.f;
		}
		if (!bReady) Manager.ReleaseGrab(ID, Customer.Get());
		else if (const auto* Product = Manager.FindProductDefinition(Bottle->ProductID); Product && Product->StaticMesh)
		{
			const USkeletalMeshComponent* Mesh = Customer->GetMesh();
			const FBox Bounds = Product->StaticMesh->GetBoundingBox();
			FVector Neck = Bounds.GetCenter();
			Neck.Z = Bounds.Max.Z;
			FVector Grip = Neck;
			Grip.Z = FMath::Lerp(Bounds.Min.Z, Bounds.Max.Z, .45);

			const FQuat Upright = FRotator(0, Customer->GetActorRotation().Yaw, 0).Quaternion();
			const FVector GripLocation = Mesh->GetSocketLocation(GripSocket);
			Bottle->SetActorLocationAndRotation(GripLocation - Upright.RotateVector(Grip * Bottle->GetActorScale3D()),
				Upright, false, nullptr, ETeleportType::TeleportPhysics);
			BottleGripInSocket = Bottle->GetRootComponent()->GetRelativeTransform();
			const FTransform BottleInHand = Bottle->GetActorTransform().GetRelativeTransform(
				Mesh->GetSocketTransform(RightHandBone));
			BottleMouthInHand = FTransform(BottleInHand.GetRotation(), BottleInHand.TransformPosition(Neck));
		}
		return bReady;
	});
	if (Result != EWalletOperationResult::Applied) return false;
	BottleISID = ID;
	return true;
}

void UColaTableCustomerComponent::Drink(APropManager& Manager, AAIController& AI, double Now)
{
	const auto* DrinkData = Manager.FindUniqueData(BottleISID);
	auto* Bottle = Manager.GetActiveActorFromISID(BottleISID);
	if (!Bottle || !DrinkData || Bottle->InteractionOwner != Customer.Get())
	{
		if (Bottle && Bottle->InteractionOwner == Customer.Get()) Manager.ReleaseGrab(BottleISID, Customer.Get());
		BottleISID = 0;
		AI.StopMovement();
		ChangeState(EColaTableCustomerState::ApproachingTable, Now);
		return;
	}

	WalkAround(AI, Now);
	if (SipStartedAt < 0)
	{
		SipStartedAt = Now + FMath::Max(0.8f, SecondsBetweenSips);
		bSipTaken = false;
		GetOwner()->ForceNetUpdate();
	}
	const double Elapsed = Now - SipStartedAt;
	if (!bSipTaken && Elapsed >= ConsumeAfter)
	{
		if (!Manager.ConsumeInstancePortions(BottleISID, Customer.Get(), PortionsPerSip))
		{
			Manager.ReleaseGrab(BottleISID, Customer.Get());
			BottleISID = 0;
			AI.StopMovement();
			ChangeState(EColaTableCustomerState::ApproachingTable, Now);
			return;
		}
		// The final sip can switch the empty bottle to another active actor class.
		if (auto* Current = Manager.GetActiveActorFromISID(BottleISID); Current && Current != Bottle
			&& Current->GetAttachParentActor() == Customer.Get())
		{
			Current->GetRootComponent()->SetRelativeTransform(BottleGripInSocket);
		}
		bSipTaken = true;
	}
	if (Elapsed < SipDuration) return;
	const auto* AfterSip = Manager.FindUniqueData(BottleISID);
	SipStartedAt = -1.0;
	if (!AfterSip || AfterSip->EventData.ContentLevel <= UE_SMALL_NUMBER)
	{
		AI.StopMovement();
		Customer->GetCharacterMovement()->StopMovementImmediately();
		ChangeState(EColaTableCustomerState::ApproachingBin, Now);
	}
}

void UColaTableCustomerComponent::ThrowBottle(APropManager& Manager, double Now)
{
	auto* Bottle = Manager.GetActiveActorFromISID(BottleISID);
	if (Bottle && Bottle->InteractionOwner == Customer.Get())
	{
		// The mesh has evaluated the forward swing before this tick. Start physics at the attached pose.
		BottleReleasePose = Bottle->GetActorTransform();
		LastBottleLocation = BottleReleasePose.GetLocation();
		const FVector Opening = BottleBin->GetOpeningLocation();
		const FVector Gravity(0, 0, GetWorld()->GetGravityZ());
		const double Damping = Bottle->PropMesh ? Bottle->PropMesh->GetLinearDamping() : 0.0;
		double VelocityIntegral, GravityIntegral;
		GetThrowFlightIntegrals(ThrowFlightTime, Damping, VelocityIntegral, GravityIntegral);
		BottleReleaseVelocity = (Opening - LastBottleLocation - Gravity * GravityIntegral) / VelocityIntegral;
		if (Manager.ReleaseGrab(BottleISID, Customer.Get()))
		{
			Bottle->SetPropState(EPropState::InMotion);
			if (auto* Body = Bottle->PropMesh.Get(); Body && Body->IsSimulatingPhysics())
			{
				Body->SetWorldLocationAndRotation(LastBottleLocation, BottleReleasePose.GetRotation(), false, nullptr,
					ETeleportType::TeleportPhysics);
				// The body can inherit the swinging hand's kinematic velocity when simulation starts.
				Body->SetPhysicsLinearVelocity(BottleReleaseVelocity);
				Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			}
			ThrowStartedAt = Now - ColaCustomerThrow::ReleaseAt;
			ChangeState(EColaTableCustomerState::ThrowingBottle, Now);
			NextActionTime = Now + ThrowFlightTime + 1.0;
			return;
		}
	}
	BottleISID = 0;
	ChangeState(EColaTableCustomerState::ApproachingTable, Now);
}

void UColaTableCustomerComponent::TrackThrownBottle(APropManager& Manager, double Now)
{
	const auto* Record = Manager.FindISIDData(BottleISID);
	if (Record && !Record->RuntimeData.bInteractionLock && Now < NextActionTime)
	{
		const auto* Bottle = Record->ActiveActor.Get();
		const FVector Current = Bottle ? Bottle->GetActorLocation() : Manager.GetTransform(BottleISID).GetLocation();
		if (!BottleBin->CrossedOpening(LastBottleLocation, Current))
		{
			LastBottleLocation = Current;
			return;
		}
		const auto* Contents = Manager.FindUniqueData(BottleISID);
		if (!Contents || Contents->EventData.ContentLevel <= UE_SMALL_NUMBER) Manager.ReleaseInstanceToEmpty(BottleISID);
	}
	BottleISID = 0;
	ChangeState(EColaTableCustomerState::ApproachingTable, Now);
}

void UColaTableCustomerComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	if (!Customer.IsValid()) return;
	if (GetNetMode() == NM_Client)
	{
		if (VisitState == EColaTableCustomerState::ThrowingBottle) SyncClientBottleRelease();
		else UpdateClientBottleGrip();
		return;
	}
	if (!Tabletop.IsValid() || !IsValid(BottleBin)) return;
	auto* AI = Cast<AAIController>(Customer->GetController());
	if (!AI) return;
	if (Controller.Get() != AI)
	{
		Controller = AI;
		if (auto* Brain = AI->GetBrainComponent()) Brain->StopLogic(TEXT("Table customer component controls this visit"));
		AI->StopMovement();
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FBox TableBounds = Tabletop->Bounds.GetBox();
	if (VisitState == EColaTableCustomerState::ApproachingTable)
	{
		if (Approach(*AI, TableBounds, Now))
		{
			Face(TableBounds.GetCenter());
			ChangeState(EColaTableCustomerState::WaitingAtTable, Now);
			NextActionTime = Now + TableWaitSeconds;
		}
		return;
	}

	auto* Manager = APropManager::Get(this);
	if (!Manager) return;
	switch (VisitState)
	{
	case EColaTableCustomerState::WaitingAtTable:
		if (Now >= NextActionTime && AI->GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			FShopProductScanData Quote;
			if (const int32 ID = FindBottle(*Manager, Quote))
			{
				if (Manager->ValidateGrab(ID, Customer.Get()))
				{
					if (BuyBottle(*Manager, Quote))
					{
						ChangeState(EColaTableCustomerState::Drinking, Now);
						break;
					}
				}
				else
				{
					const FVector Location = Manager->GetTransform(ID).GetLocation();
					bHasApproachTarget = false;
					Approach(*AI, TableBounds, Now, &Location);
				}
			}
			NextActionTime = Now + ScanInterval;
		}
		break;
	case EColaTableCustomerState::Drinking:
		Drink(*Manager, *AI, Now);
		break;
	case EColaTableCustomerState::ApproachingBin:
	{
		const FBox Bounds = BottleBin->BinMesh->Bounds.GetBox();
		const FVector Extent = Bounds.GetExtent();
		const float CustomerRadius = Customer->GetSimpleCollisionRadius();
		const double DistanceSquared = FVector::DistSquared2D(Customer->GetActorLocation(), BottleBin->GetOpeningLocation());
		const double TossRange = CustomerRadius * 6.5 + FMath::Max(Extent.X, Extent.Y);
		const bool bInTossRange = DistanceSquared <= FMath::Square(TossRange)
			&& DistanceSquared >= FMath::Square(CustomerRadius + 100.f);
		if (bInTossRange || Approach(*AI, Bounds, Now))
		{
			AI->StopMovement();
			Customer->GetCharacterMovement()->StopMovementImmediately();
			Face(BottleBin->GetOpeningLocation());
			ThrowStartedAt = Now + ThrowStartLead;
			ChangeState(EColaTableCustomerState::PreparingThrow, Now);
		}
		break;
	}
	case EColaTableCustomerState::PreparingThrow:
		if (Now >= ThrowStartedAt + ColaCustomerThrow::ReleaseAt) ThrowBottle(*Manager, Now);
		break;
	case EColaTableCustomerState::ThrowingBottle:
		TrackThrownBottle(*Manager, Now);
		break;
	default:
		break;
	}
}
