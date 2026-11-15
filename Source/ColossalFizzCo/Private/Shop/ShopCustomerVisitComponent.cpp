#include "Shop/ShopCustomerVisitComponent.h"
#include "Shop/NormalColaNavigationConfig.h"
#include "Shop/ShopCustomerFlow.h"
#include "Shop/ShopCustomerCashComponent.h"
#include "Shop/ShopScannerComponent.h"
#include "Shop/ShopCheckoutComponent.h"
#include "Shop/ShopInteractionGeometry.h"
#include "Props/InteractablePropManager.h"
#include "Props/InteractableProp.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"

UShopCustomerVisitComponent::UShopCustomerVisitComponent() { PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
void UShopCustomerVisitComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UShopCustomerVisitComponent,Phase); DOREPLIFETIME(UShopCustomerVisitComponent,Status);
	DOREPLIFETIME(UShopCustomerVisitComponent,Bucket);
	DOREPLIFETIME(UShopCustomerVisitComponent,BucketOriginalCollision);
	DOREPLIFETIME(UShopCustomerVisitComponent,BucketOriginalPawnResponse);
}
void UShopCustomerVisitComponent::OnRep_Phase()
{
	OnRep_BucketState();
	const bool Hidden=Phase==EShopVisitPhase::Inactive;
	GetOwner()->SetActorHiddenInGame(Hidden); GetOwner()->SetActorEnableCollision(!Hidden);
	TArray<UTextRenderComponent*> Labels; GetOwner()->GetComponents(Labels);
	for (auto* Label:Labels) if (Label->ComponentHasTag(TEXT("ShopVisitStatus"))) Label->SetText(FText::FromString(Status));
}
void UShopCustomerVisitComponent::OnRep_BucketState()
{
	if (GetOwner()->HasAuthority()) return;
	if (PresentedBucket!=Bucket)
	{
		if (PresentedBucket.IsValid()) if (auto* Body=Cast<UPrimitiveComponent>(PresentedBucket->GetRootComponent()))
		{
			Body->SetCollisionResponseToChannel(ECC_Pawn,PresentedOriginalPawnResponse);
			Body->SetCollisionEnabled(PresentedOriginalCollision);
		}
		PresentedBucket=Bucket;
	}
	PresentedOriginalCollision=BucketOriginalCollision;
	PresentedOriginalPawnResponse=BucketOriginalPawnResponse;
	if (Bucket.IsValid()) if (auto* Body=Cast<UPrimitiveComponent>(Bucket->GetRootComponent()))
	{
		const bool Carried=Phase==EShopVisitPhase::Shopping || Phase==EShopVisitPhase::Queuing || Phase==EShopVisitPhase::ReturningBucket;
		Body->SetSimulatePhysics(false);
		Body->SetCollisionResponseToChannel(ECC_Pawn,Carried ? ECR_Ignore : BucketOriginalPawnResponse.GetValue());
		Body->SetCollisionEnabled(Carried ? ECollisionEnabled::QueryOnly : BucketOriginalCollision.GetValue());
	}
}

void UShopCustomerVisitComponent::SetPhase(EShopVisitPhase Next,const FString& Message)
{
	Phase=Next; Status=Message; OnRep_Phase(); GetOwner()->ForceNetUpdate();
}
bool UShopCustomerVisitComponent::StartVisit(AShopCustomerFlow* InFlow,AActor* InBucket)
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Phase!=EShopVisitPhase::Inactive || !IsValid(InFlow) || !IsValid(InBucket)) return false;
	Manager=APropManager::Get(this); Cash=GetOwner()->FindComponentByClass<UShopCustomerCashComponent>();
	if (!Manager.IsValid() || !Cash.IsValid() || !Cash->CustomerID.IsValid() || !Manager->ShopCheckout) return false;
	Flow=InFlow; Bucket=InBucket;
	if (auto* Body=Cast<UPrimitiveComponent>(InBucket->GetRootComponent()))
	{
		bBucketOriginallyAffectedNavigation=Body->CanEverAffectNavigation();
		BucketOriginalCollision=Body->GetCollisionEnabled();
		BucketOriginalPawnResponse=Body->GetCollisionResponseToChannel(ECC_Pawn);
	} Collected.Empty(); RequestedTypes.Empty(); Wanted.Empty(); Rejected.Empty(); TargetISID=0; CheckoutID.Invalidate();
	Cash->BuildShoppingPlan(InFlow->BottlesPerCustomer); Wanted=Cash->GetPlannedContents();
	if (Wanted.IsEmpty()) return false;
	CaptureCarryMovement();
	InFlow->PrepareCustomerMovement(Cast<ACharacter>(GetOwner()));
	SetPhase(EShopVisitPhase::Entering,TEXT("Entering shop")); MoveTo(InFlow->EntrancePoint->GetActorLocation());
	GetWorld()->GetTimerManager().SetTimer(VisitTimer,this,&UShopCustomerVisitComponent::AdvanceVisit,.25f,true);
	return true;
}
bool UShopCustomerVisitComponent::MoveTo(FVector Destination)
{
	auto* Pawn=Cast<ACharacter>(GetOwner()); auto* Controller=Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	auto* Nav=UNavigationSystemV1::GetCurrent(GetWorld()); FNavLocation Projected;
	bMoving=false; bMoveFailed=true; MoveStarted=GetWorld()->GetTimeSeconds(); LastMoveAttempt=MoveStarted; MoveDestination=Destination; MoveFailureReason.Empty();
	RouteTimeBudget=FMath::Max(5.f,MoveTimeoutSeconds); LastRouteProgressTime=MoveStarted; LastRouteProgressPosition=GetOwner()->GetActorLocation();
	if (!Controller) { MoveFailureReason=TEXT("No AI controller"); return false; }
	if (!Nav) { MoveFailureReason=TEXT("No navigation system"); return false; }
	// Query at the customer's floor rather than selecting a disconnected shelf-top nav island.
	Destination.Z=Pawn->GetNavAgentLocation().Z;
	const float Extent=Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+400;
	const auto* NavData=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	const auto Filter=NavData ? UNavigationQueryFilter::GetQueryFilter(*NavData,Controller,UNormalColaCustomerFilter::StaticClass()) : FSharedConstNavQueryFilter();
	if (!NavData || !Nav->ProjectPointToNavigation(Destination,Projected,FVector(Extent,Extent,200),NavData,Filter)) { MoveFailureReason=TEXT("No navigation polygon near destination"); return false; }
	MoveGoal=Projected.Location;
	const float Acceptance=Phase==EShopVisitPhase::Queuing ? FMath::Max(1.f,QueueAcceptanceRadius) : Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+60.f;
	const auto Result=Controller->MoveToLocation(MoveGoal,Acceptance,false,true,false,true,UNormalColaCustomerFilter::StaticClass(),false);
	bMoveFailed=Result==EPathFollowingRequestResult::Failed;
	if (bMoveFailed) MoveFailureReason=TEXT("Path request failed");
	bMoving=!bMoveFailed;
	if (bMoving) if (const auto* Follow=Controller->GetPathFollowingComponent()) if (const auto Path=Follow->GetPath())
	{
		const auto* Movement=Pawn->GetCharacterMovement();
		const double Speed=FMath::Max(1.f,Movement->MaxWalkSpeed);
		const double TurnSeconds=180./FMath::Max(1.f,FMath::Abs(Movement->RotationRate.Yaw));
		// The unchanged giant layout can require a long detour. Allow travel and
		// turning at the actual carry speed, but retain a finite overall deadline.
		RouteTimeBudget=FMath::Clamp(Path->GetLength()/Speed*2.+TurnSeconds,RouteTimeBudget,
			FMath::Max(RouteTimeBudget,double(MaxRouteDurationSeconds)));
	}
	return bMoving;
}
bool UShopCustomerVisitComponent::Arrived() const
{
	const auto* Pawn=Cast<ACharacter>(GetOwner());
	if (Pawn && Phase==EShopVisitPhase::Shopping && Manager.IsValid())
	{
		const auto* Record=Manager->FindISIDData(TargetISID);
		const auto* Product=Record ? Manager->FindProductDefinition(Record->ProductID) : nullptr;
		if (Product && Product->StaticMesh)
		{
			const auto Bounds=Product->StaticMesh->GetBoundingBox().TransformBy(Manager->GetTransform(TargetISID));
			return ShopInteractionGeometry::DistanceToCustomer(Pawn,Bounds.GetClosestPointTo(Pawn->GetActorLocation()))<=FMath::Max(0.f,ShoppingReach);
		}
	}
	const float Acceptance=Phase==EShopVisitPhase::Queuing ? FMath::Max(1.f,QueueAcceptanceRadius)+5 : (Pawn ? Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+100.f : 0.f);
	return bMoving && Pawn && FVector::DistSquared2D(Pawn->GetNavAgentLocation(),MoveGoal)<=FMath::Square(Acceptance)
		&& FMath::Abs(Pawn->GetNavAgentLocation().Z-MoveGoal.Z)<200;
}
USceneComponent* UShopCustomerVisitComponent::NextPocket() const
{
	if (!Bucket.IsValid()) return nullptr;
	TArray<USceneComponent*> Pockets; Bucket->GetComponents(Pockets);
	Pockets.RemoveAll([](const auto* Part){return !Part->ComponentHasTag(TEXT("ShopBucketPocket"));});
	Pockets.Sort([](const auto& A,const auto& B){return A.GetFName().LexicalLess(B.GetFName());});
	return Pockets.IsValidIndex(Collected.Num()) ? Pockets[Collected.Num()] : nullptr;
}
void UShopCustomerVisitComponent::ChooseNextProduct()
{
	TargetISID=0; double Best=TNumericLimits<double>::Max();
	if (!NextPocket()) Wanted.Empty();
	for (int32 ID:Manager->ShopCheckout->GetAvailableColaStockISIDs())
	{
		const auto* Record=Manager->FindISIDData(ID);
		FShopProductScanData Candidate;
  if (!Record || !Manager->GetProductScanData(ID,Candidate) || Wanted.FindRef(FIntPoint(Candidate.ContentID,Candidate.NominalPortions))<=0 || Rejected.Contains(ID) || Collected.Contains(ID) || Flow->StockIsClaimed(ID) || Record->State==EPropState::Held) continue;
		const double Distance=FVector::DistSquared2D(GetOwner()->GetActorLocation(),Manager->GetTransform(ID).GetLocation());
		if (Distance<Best) { Best=Distance; TargetISID=ID; }
	}
	if (TargetISID && Flow->ClaimStock(TargetISID,this))
	{
		SetPhase(EShopVisitPhase::Shopping,TEXT("Collecting Normal Cola")); MoveTo(Manager->GetTransform(TargetISID).GetLocation()); return;
	}
	TargetISID=0;
	if (Collected.IsEmpty()) { AbortVisit(TEXT("No requested stock available")); return; }
	Flow->JoinQueue(this); SetPhase(EShopVisitPhase::Queuing,TEXT("Queuing for checkout")); MoveTo(Flow->QueueLocation(Flow->QueuePosition(this)));
}
void UShopCustomerVisitComponent::CarryBucket()
{
	const auto* Character=Cast<ACharacter>(GetOwner());
	if (!Bucket.IsValid() || !Character) return;
	auto* Body=Cast<UPrimitiveComponent>(Bucket->GetRootComponent());
	if (Body)
	{
		Body->SetSimulatePhysics(false); Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); Body->SetCanEverAffectNavigation(false);
	}
	const auto Bounds=Body ? Body->CalcBounds(FTransform(FQuat::Identity,FVector::ZeroVector,Bucket->GetActorScale3D())) : FBoxSphereBounds(FVector::ZeroVector,FVector(50),50);
	auto* Capsule=Character->GetCapsuleComponent();
	Capsule->IgnoreActorWhenMoving(Bucket.Get(),true);
	Bucket->AttachToComponent(GetOwner()->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
	Bucket->SetActorTransform(ShopInteractionGeometry::CarryContainerTransform(Character,Bounds,Bucket->GetActorScale3D()),false,nullptr,ETeleportType::TeleportPhysics);
	ApplyGentleCarryMovement(Bounds);
}
void UShopCustomerVisitComponent::AdvanceVisit()
{
	if (!GetOwner()->HasAuthority() || GetNetMode()==NM_Client || Phase==EShopVisitPhase::Inactive || Phase==EShopVisitPhase::Departed || Phase==EShopVisitPhase::Aborted) return;
	if (!Flow.IsValid() || !Manager.IsValid() || !Cash.IsValid()) { AbortVisit(TEXT("Customer visit services unavailable")); return; }
	const bool MovingPhase=Phase==EShopVisitPhase::Entering || Phase==EShopVisitPhase::GettingBucket || Phase==EShopVisitPhase::Shopping
		|| Phase==EShopVisitPhase::ReturningBucket || Phase==EShopVisitPhase::Departing;
	// A dynamic navigation rebuild can invalidate an accepted path while the
	// controller becomes idle. Retry at a bounded rate without resetting the
	// overall route deadline or moving the customer outside navigation.
	if (MovingPhase && !Arrived() && GetWorld()->GetTimeSeconds()-LastMoveAttempt>=2.)
	{
		const auto* Pawn=Cast<APawn>(GetOwner());
		const auto* Controller=Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		if (bMoveFailed || (Controller && Controller->GetMoveStatus()==EPathFollowingStatus::Idle))
		{
			const double RouteStarted=MoveStarted, Budget=RouteTimeBudget, ProgressTime=LastRouteProgressTime;
			const FVector ProgressPosition=LastRouteProgressPosition;
			MoveTo(MoveDestination); MoveStarted=RouteStarted; RouteTimeBudget=Budget;
			LastRouteProgressTime=ProgressTime; LastRouteProgressPosition=ProgressPosition;
		}
	}
	if (MovingPhase && FVector::DistSquared2D(GetOwner()->GetActorLocation(),LastRouteProgressPosition)>FMath::Square(50.))
	{
		LastRouteProgressPosition=GetOwner()->GetActorLocation(); LastRouteProgressTime=GetWorld()->GetTimeSeconds();
	}
	if (MovingPhase && !Arrived() && (GetWorld()->GetTimeSeconds()-MoveStarted>RouteTimeBudget
		|| GetWorld()->GetTimeSeconds()-LastRouteProgressTime>MoveTimeoutSeconds))
	{
		if (CheckoutID.IsValid() && (Phase==EShopVisitPhase::ReturningBucket || Phase==EShopVisitPhase::Departing))
		{
			// Payment is already committed. A temporarily blocked exit must not
			// cancel the visit or strand purchased goods after the obstacle clears.
			MoveTo(MoveDestination);
			Status=TEXT("Please clear my exit route"); OnRep_Phase(); GetOwner()->ForceNetUpdate();
		}
		else if (Phase==EShopVisitPhase::Shopping) { Rejected.Add(TargetISID); ChooseNextProduct(); }
		else { UE_LOG(LogTemp,Warning,TEXT("Customer route failure phase=%d reason=%s"),int32(Phase),*MoveFailureReason); AbortVisit(TEXT("Customer route is blocked")); }
		return;
	}
	switch (Phase)
	{
	case EShopVisitPhase::Entering:
		if (Arrived()) { SetPhase(EShopVisitPhase::GettingBucket,TEXT("Getting shopping bucket")); MoveTo(Bucket->GetActorLocation()); }
		break;
	case EShopVisitPhase::GettingBucket:
		if (Arrived())
		{
			if (!Bucket.IsValid()) { AbortVisit(TEXT("Shopping bucket unavailable")); break; }
			CarryBucket(); ChooseNextProduct();
		}
		break;
	case EShopVisitPhase::Shopping:
		if (Arrived())
		{
			const auto* Record=Manager->FindISIDData(TargetISID); const int32 PID=Record ? Record->ProductID : 0;
			// Arrived validated this bottle's bounds against ShoppingReach on the server.
			FShopProductScanData Requested;
			AActivePropActor* Prop=nullptr; auto* Pocket=NextPocket();
			if (Manager->GetProductScanData(TargetISID,Requested) && Pocket && Record && Record->State!=EPropState::Held && !Record->RuntimeData.bInteractionLock && Record->PackedRuntimeData.bIsGrabbable
				&& Manager->ShopCheckout->IsAvailableColaStockISID(TargetISID)
				&& Manager->BeginGrab(TargetISID,GetOwner(),Pocket) && (Prop=Manager->GetActiveActorFromISID(TargetISID)))
			{
				RecordCollected(TargetISID,Requested); --Wanted.FindOrAdd(FIntPoint(Requested.ContentID,Requested.NominalPortions));
				if (Prop->PropMesh) { Prop->PropMesh->SetSimulatePhysics(false); Prop->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
			}
			else Rejected.Add(TargetISID);
			ChooseNextProduct();
		}
		break;
	case EShopVisitPhase::Queuing:
	{
		const int32 Index=Flow->QueuePosition(this);
		if (Index==INDEX_NONE) { AbortVisit(TEXT("Queue assignment lost")); break; }
		// A stationary customer ahead can obstruct a valid path for an arbitrarily
		// long checkout. Waiting in line is not a failed shopping route.
		const double SinceMove=GetWorld()->GetTimeSeconds()-MoveStarted;
		if (SinceMove>=2 && (bMoveFailed || FVector::DistSquared2D(MoveGoal,Flow->QueueLocation(Index))>FMath::Square(120.f)
			|| (!Arrived() && SinceMove>MoveTimeoutSeconds))) MoveTo(Flow->QueueLocation(Index));
		if (Index!=0 || !Arrived()) break;
		auto* Scanner=Flow->GetScanner(); auto* Dock=Flow->GetBucketDock();
		if (!Scanner || !Dock || Cash->JoinScanner(Scanner)!=EShopCheckoutResult::Success) break;
		Bucket->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); Bucket->SetActorLocationAndRotation(Dock->GetComponentLocation(),Dock->GetComponentRotation(),false,nullptr,ETeleportType::TeleportPhysics);
		if (auto* Body=Cast<UPrimitiveComponent>(Bucket->GetRootComponent()))
		{ Body->SetCollisionResponseToChannel(ECC_Pawn,BucketOriginalPawnResponse); Body->SetCollisionEnabled(BucketOriginalCollision); }
		for (int32 ID:Collected) if (auto* Prop=Manager->GetActiveActorFromISID(ID)) Prop->DetachForInteraction();
		CheckoutServiceLocation=GetOwner()->GetActorLocation();
		SetPhase(EShopVisitPhase::WaitingForCashier,TEXT("Please scan my bottles and complete checkout"));
		break;
	}
	case EShopVisitPhase::WaitingForCashier:
	{
		FCompletedCheckoutReceipt Receipt;
		if (Cash->FindCompletedVisitReceipt(Receipt))
		{
			CheckoutID=Receipt.CheckoutID; SetPhase(EShopVisitPhase::CollectingPurchase,TEXT("Collecting purchased bottles"));
		}
		break;
	}
	case EShopVisitPhase::CollectingPurchase:
		if (CollectPaidGoods())
		{
			ReturnUnsoldGoods();
			if (!Cash->ReleaseStation(false)) break;
			CarryBucket();
			SetPhase(EShopVisitPhase::ReturningBucket,TEXT("Returning empty bucket at exit")); MoveTo(Flow->BucketReturnPoint->GetActorLocation());
		}
		break;
	case EShopVisitPhase::ReturningBucket:
		// Keep the next customer behind the cashier slot until this customer's
		// capsule has cleared it. Releasing at payment lets the queue cut off
		// a departing customer's route and deadlock both giant capsules.
		if (Flow->QueuePosition(this)!=INDEX_NONE)
		{
			const auto* Character=Cast<ACharacter>(GetOwner());
			const float Clearance=Character ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius()*2+60 : 468.f;
			bool ContainerClear=true;
			const auto* Body=Bucket.IsValid() ? Cast<UPrimitiveComponent>(Bucket->GetRootComponent()) : nullptr;
			const auto* Dock=Flow->GetBucketDock();
			if (Body && Dock)
			{
				const auto DockBounds=Body->CalcBounds(FTransform(Dock->GetComponentQuat(),Dock->GetComponentLocation(),Bucket->GetActorScale3D()));
				ContainerClear=!Body->Bounds.GetBox().Intersect(DockBounds.GetBox().ExpandBy(50.));
			}
			if (ContainerClear && FVector::DistSquared2D(GetOwner()->GetActorLocation(),CheckoutServiceLocation)>FMath::Square(Clearance))
				Flow->ReleaseVisit(this);
		}
		if (Arrived()) { Flow->ReleaseVisit(this); ReleaseBucket(); SetPhase(EShopVisitPhase::Departing,TEXT("Leaving with purchases")); MoveTo(Flow->DeparturePoint->GetActorLocation()); }
		break;
	case EShopVisitPhase::Departing:
		if (Arrived()) { SetPhase(EShopVisitPhase::Departed,TEXT("Visit completed")); GetWorld()->GetTimerManager().ClearTimer(VisitTimer); }
		break;
	default: break;
	}
}

bool UShopCustomerVisitComponent::CollectPaidGoods()
{
	for (const auto& Case:Manager->ShopCheckout->PendingExpiryCases)
		if (Case.CheckoutID==CheckoutID && Case.Choice==EShopCompensationChoice::Unselected) { Status=TEXT("Choose expiry compensation to finish service"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
	const auto Goods=Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID);
	for (int32 ID:Goods)
	{
		const auto* Record=Manager->FindISIDData(ID);
		if (!Record) { Status=TEXT("A purchased bottle is missing"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
		if (Record->State==EPropState::Held && Record->RuntimeData.InteractionOwner.IsValid() && Record->RuntimeData.InteractionOwner.Get()!=GetOwner())
		{ Status=TEXT("Please release my purchased bottles"); OnRep_Phase(); GetOwner()->ForceNetUpdate(); return false; }
	}
	int32 Index=0;
	for (int32 ID:Goods)
	{
		if (!Manager->BeginGrab(ID,GetOwner(),GetOwner()->GetRootComponent())) return false;
		if (auto* Prop=Manager->GetActiveActorFromISID(ID))
		{
			Prop->SetActorRelativeLocation(FVector(40+(Index%2)*20,(Index/2)*20,15),false,nullptr,ETeleportType::TeleportPhysics);
			if (Prop->PropMesh) { Prop->PropMesh->SetSimulatePhysics(false); Prop->PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
		}
		++Index;
	}
	return true;
}

bool UShopCustomerVisitComponent::AcceptsInvoiceItems(const TArray<FCheckoutItemQuote>& Items) const
{
	if (Phase!=EShopVisitPhase::WaitingForCashier || !Manager.IsValid() || Items.Num()!=Collected.Num()) return false;
	// The requested contents are immutable even if the original bottle spills or is replaced.
	TMap<FIntPoint,int32> Required=RequestedTypes;
	for (const auto& Item:Items)
	{
		auto* Count=Required.Find(FIntPoint(Item.ContentID,Item.Snapshot.NominalPortions));
		if (!Count || *Count<=0) return false;
		--*Count;
	}
	return true;
}
void UShopCustomerVisitComponent::ReturnUnsoldGoods()
{
	if (!Manager.IsValid()) return;
	for (int32 ID:Collected)
	{
		FShopStockEntry Stock;
		if (!Manager->ShopCheckout->GetStockEntry(ID,Stock) || Stock.CustomerOwner.IsValid()) continue;
		if (auto* Prop=Manager->GetActiveActorFromISID(ID))
		{
			const auto* Record=Manager->FindISIDData(ID);
			if (Record && Record->RuntimeData.InteractionOwner.Get()==GetOwner()) Prop->DetachForInteraction();
			Manager->ConvertASMToISM(ID,EPropState::AtRest,true,true);
		}
		Manager->InitializeInstanceTransform(ID,Stock.StockTransform);
	}
}
void UShopCustomerVisitComponent::ReleaseBucket()
{
	if (auto* Character=Cast<ACharacter>(GetOwner()); Character && Bucket.IsValid()) Character->GetCapsuleComponent()->IgnoreActorWhenMoving(Bucket.Get(),false);
	if (Flow.IsValid() && Bucket.IsValid()) Flow->ReturnBucket(Bucket.Get(),this);
	if (Bucket.IsValid()) if (auto* Body=Cast<UPrimitiveComponent>(Bucket->GetRootComponent()))
	{
		Body->SetCollisionResponseToChannel(ECC_Pawn,BucketOriginalPawnResponse);
		Body->SetCollisionEnabled(BucketOriginalCollision);
		Body->SetCanEverAffectNavigation(bBucketOriginallyAffectedNavigation);
	}
	Bucket.Reset();
}
void UShopCustomerVisitComponent::AbortVisit(const FString& Reason)
{
	if (!GetOwner()->HasAuthority() || bAborting) return;
	TGuardValue<bool> Guard(bAborting,true);
	if (Cash.IsValid() && !Cash->ReleaseStation(true)) { Status=Reason+TEXT("; resolve checkout compensation"); OnRep_Phase(); return; }
	if (auto* Pawn=Cast<APawn>(GetOwner())) if (auto* AI=Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
	ReturnUnsoldGoods(); ReleaseBucket();
	if (Flow.IsValid()) Flow->ReleaseVisit(this);
	RestoreCarryMovement();
	SetPhase(EShopVisitPhase::Aborted,Reason); GetWorld()->GetTimerManager().ClearTimer(VisitTimer);
	UE_LOG(LogTemp,Warning,TEXT("Shop visit aborted for %s: %s"),*GetNameSafe(GetOwner()),*Reason);
}
bool UShopCustomerVisitComponent::RetireVisit()
{
	if (!GetOwner()->HasAuthority() || !Manager.IsValid() || !Cash.IsValid()
		|| (Phase!=EShopVisitPhase::Departed && (Phase!=EShopVisitPhase::Aborted || CheckoutID.IsValid()))) return false;
	if (!Cash->ReleaseStation(true)) return false;
	// Financial stock/receipt records remain immutable; only the departed customer's world representations retire.
	for (int32 ID:Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID))
	{
		if (const auto* Unique=Manager->FindUniqueData(ID))
		{
			const int32 Cap=Unique->EventData.AttachedCapISID;
			if (Cap>0) Manager->ReleaseInstanceToEmpty(Cap);
		}
		Manager->ReleaseInstanceToEmpty(ID);
	}
	if (!Cash->ResetVisit()) return false;
	RestoreCarryMovement();
	Collected.Empty(); RequestedTypes.Empty(); Wanted.Empty(); CheckoutID.Invalidate();
	SetPhase(EShopVisitPhase::Inactive,TEXT("")); return true;
}
void UShopCustomerVisitComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	auto* ComponentWorld=GetWorld();
	if (ComponentWorld) ComponentWorld->GetTimerManager().ClearTimer(VisitTimer);
	if (ComponentWorld && GetOwner() && GetOwner()->HasAuthority() && !ComponentWorld->bIsTearingDown)
	{
		if (Cash.IsValid()) Cash->ReleaseStation(true);
		ReturnUnsoldGoods(); ReleaseBucket(); if (Flow.IsValid()) Flow->ReleaseVisit(this);
		if (Manager.IsValid() && Cash.IsValid())
		{
			for (int32 ID:Manager->ShopCheckout->GetCustomerOwnedStockISIDs(Cash->CustomerID)) if (auto* Prop=Manager->GetActiveActorFromISID(ID)) if (Prop->InteractionOwner==GetOwner()) Prop->DetachForInteraction();
			Manager->ShopCheckout->ReclaimCustomerCoins(Cash->CustomerID,Manager->ShopCheckout->GetCustomerCoinISIDs(Cash->CustomerID));
		}
	}
	RestoreCarryMovement();
	Super::EndPlay(Reason);
}

FString UShopCustomerVisitComponent::DescribeRoute() const
{
	const auto* Scanner=Flow.IsValid() ? Flow->GetScanner() : nullptr;
	const double Reach=Scanner ? ShopInteractionGeometry::CustomerDistanceToStation(GetOwner(),Scanner->GetOwner()) : -1;
	FString Blocking;
	int32 FollowStatus=-1;
	FVector SegmentGoal=MoveGoal,Velocity=FVector::ZeroVector;
	if (const auto* Character=Cast<ACharacter>(GetOwner()))
	{
		Velocity=Character->GetVelocity();
		if (const auto* Controller=Cast<AAIController>(Character->GetController()))
		{
			FollowStatus=int32(Controller->GetMoveStatus());
			if (const auto* Follow=Controller->GetPathFollowingComponent()) SegmentGoal=Follow->GetCurrentTargetLocation();
		}
		const auto* Capsule=Character->GetCapsuleComponent();
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ShopVisitRouteDiagnostic),false,GetOwner());
		if (Bucket.IsValid()) Query.AddIgnoredActor(Bucket.Get());
		const FVector Start=Capsule->GetComponentLocation();
		const FVector Direction=(SegmentGoal-Start).GetSafeNormal2D();
		if (GetWorld()->SweepSingleByChannel(Hit,Start,Start+Direction*60,Capsule->GetComponentQuat(),ECC_Pawn,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))
			Blocking=FString::Printf(TEXT("%s.%s penetration=%d"),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.bStartPenetrating);
	}
	return FString::Printf(TEXT("pos=%s goal=%s arrived=%d moving=%d failed=%d reach=%.1f reason=%s follow=%d blocker=%s"),*GetOwner()->GetActorLocation().ToCompactString(),*MoveGoal.ToCompactString(),Arrived(),bMoving,bMoveFailed,Reach,*MoveFailureReason,FollowStatus,*Blocking)+FString::Printf(TEXT(" segment=%s velocity=%s route_age=%.1f budget=%.1f idle_age=%.1f"),*SegmentGoal.ToCompactString(),*Velocity.ToCompactString(),GetWorld()->GetTimeSeconds()-MoveStarted,RouteTimeBudget,GetWorld()->GetTimeSeconds()-LastRouteProgressTime)+FString::Printf(TEXT(" queue=%s"),Flow.IsValid() && Flow->QueuePoint ? *Flow->QueuePoint->GetActorLocation().ToCompactString() : TEXT("none"));
}


void UShopCustomerVisitComponent::CaptureCarryMovement()
{
	if (auto* Character=Cast<ACharacter>(GetOwner())) if (auto* Move=Character->GetCharacterMovement())
	{
		bCapturedMovement=true;
		OriginalWalkSpeed=Move->MaxWalkSpeed; OriginalAcceleration=Move->MaxAcceleration;
		OriginalBraking=Move->BrakingDecelerationWalking; OriginalRotationRate=Move->RotationRate;
		bOriginalControllerYaw=Character->bUseControllerRotationYaw;
		bOriginalOrientToMovement=Move->bOrientRotationToMovement;
		bOriginalDesiredRotation=Move->bUseControllerDesiredRotation;
	}
}
void UShopCustomerVisitComponent::ApplyGentleCarryMovement(const FBoxSphereBounds& Bounds)
{
	if (!bCapturedMovement || !Bucket.IsValid()) return;
	if (auto* Character=Cast<ACharacter>(GetOwner())) if (auto* Move=Character->GetCharacterMovement())
	{
		Move->MaxWalkSpeed=FMath::Min(OriginalWalkSpeed,FMath::Max(1.f,CarryingWalkSpeed));
		Move->MaxAcceleration=FMath::Min(OriginalAcceleration,FMath::Max(1.f,CarryingAcceleration));
		Move->BrakingDecelerationWalking=FMath::Min(OriginalBraking,FMath::Max(1.f,CarryingAcceleration));
		const double Reach=FVector::Dist2D(Character->GetActorLocation(),Bucket->GetActorLocation())+Bounds.BoxExtent.Size2D()+100;
		const float GentleYaw=FMath::RadiansToDegrees(100./FMath::Max(1.,Reach));
		Move->RotationRate.Yaw=FMath::Min(FMath::Abs(OriginalRotationRate.Yaw),GentleYaw);
		Character->bUseControllerRotationYaw=false;
		Move->bUseControllerDesiredRotation=false; Move->bOrientRotationToMovement=true;
	}
}
void UShopCustomerVisitComponent::RestoreCarryMovement()
{
	if (!bCapturedMovement) return;
	if (auto* Character=Cast<ACharacter>(GetOwner())) if (auto* Move=Character->GetCharacterMovement())
	{
		Move->MaxWalkSpeed=OriginalWalkSpeed; Move->MaxAcceleration=OriginalAcceleration;
		Move->BrakingDecelerationWalking=OriginalBraking; Move->RotationRate=OriginalRotationRate;
		Character->bUseControllerRotationYaw=bOriginalControllerYaw;
		Move->bOrientRotationToMovement=bOriginalOrientToMovement; Move->bUseControllerDesiredRotation=bOriginalDesiredRotation;
	}
	bCapturedMovement=false;
}


void UShopCustomerVisitComponent::RecordCollected(int32 ID,const FShopProductScanData& Scan)
{
	if (Collected.Contains(ID)) return;
	Collected.Add(ID);
	++RequestedTypes.FindOrAdd(FIntPoint(Scan.ContentID,Scan.NominalPortions));
}
