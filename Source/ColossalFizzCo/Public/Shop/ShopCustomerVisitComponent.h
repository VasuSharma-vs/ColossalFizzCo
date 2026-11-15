#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Shop/ShopCheckoutTypes.h"
#include "ShopCustomerVisitComponent.generated.h"
class AShopCustomerFlow;
class APropManager;
class ACharacter;
class UShopCustomerCashComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EShopVisitPhase:uint8 { Inactive,Entering,GettingBucket,Shopping,Queuing,WaitingForCashier,CollectingPurchase,ReturningBucket,Departing,Departed,Aborted };

/** Server-only visit state machine on the existing customer character; phase is replicated for presentation. */
UCLASS(ClassGroup=(Shop),meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UShopCustomerVisitComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UShopCustomerVisitComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Visit") EShopVisitPhase Phase=EShopVisitPhase::Inactive;
	UPROPERTY(ReplicatedUsing=OnRep_Phase,BlueprintReadOnly,Category="Visit") FString Status;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="5")) float MoveTimeoutSeconds=45;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="5")) float MaxRouteDurationSeconds=300;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="0",Units="cm")) float ShoppingReach=400.f;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="1",Units="cm")) float QueueAcceptanceRadius=25.f;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="1",Units="cm/s")) float CarryingWalkSpeed=220.f;
	UPROPERTY(EditAnywhere,Category="Visit",meta=(ClampMin="1",Units="cm/s^2")) float CarryingAcceleration=300.f;
	bool StartVisit(AShopCustomerFlow* InFlow,AActor* InBucket);
	void AdvanceVisit();
	FString DescribeRoute() const;
	void AbortVisit(const FString& Reason);
	bool RetireVisit();
	bool AcceptsInvoiceItems(const TArray<FCheckoutItemQuote>& Items) const;
	AShopCustomerFlow* GetFlow() const { return Flow.Get(); }
	const TArray<int32>& GetCollectedISIDs() const { return Collected; }
private:
	friend class FShopVisitClaimsTest;
	UPROPERTY(Transient) TWeakObjectPtr<AShopCustomerFlow> Flow;
	UPROPERTY(Transient,ReplicatedUsing=OnRep_BucketState) TWeakObjectPtr<AActor> Bucket;
	UPROPERTY(Transient) TWeakObjectPtr<APropManager> Manager;
	UPROPERTY(Transient) TWeakObjectPtr<UShopCustomerCashComponent> Cash;
	TArray<int32> Collected;
	TMap<FIntPoint,int32> RequestedTypes;
	void RecordCollected(int32 ISID,const FShopProductScanData& Scan);
	TMap<FIntPoint,int32> Wanted;
	TSet<int32> Rejected;
	int32 TargetISID=0;
	FGuid CheckoutID;
	FVector MoveGoal=FVector::ZeroVector;
	FVector MoveDestination=FVector::ZeroVector;
	FVector CheckoutServiceLocation=FVector::ZeroVector;
	double LastMoveAttempt=0;
	double MoveStarted=0;
	double RouteTimeBudget=45,LastRouteProgressTime=0;
	FVector LastRouteProgressPosition=FVector::ZeroVector;
	bool bMoving=false;
	bool bMoveFailed=false;
	FString MoveFailureReason;
	bool bAborting=false;
	bool bBucketOriginallyAffectedNavigation=false;
	UPROPERTY(ReplicatedUsing=OnRep_BucketState) TEnumAsByte<ECollisionEnabled::Type> BucketOriginalCollision=ECollisionEnabled::QueryAndPhysics;
	UPROPERTY(ReplicatedUsing=OnRep_BucketState) TEnumAsByte<ECollisionResponse> BucketOriginalPawnResponse=ECR_Block;
	FTimerHandle VisitTimer;
	void SetPhase(EShopVisitPhase Next,const FString& Message);
	bool MoveTo(FVector Destination);
	bool Arrived() const;
	USceneComponent* NextPocket() const;
	void CarryBucket();
	void CaptureCarryMovement();
	void ApplyGentleCarryMovement(const FBoxSphereBounds& Bounds);
	void RestoreCarryMovement();
	bool bCapturedMovement=false;
	float OriginalWalkSpeed=0,OriginalAcceleration=0,OriginalBraking=0;
	FRotator OriginalRotationRate=FRotator::ZeroRotator;
	bool bOriginalControllerYaw=false,bOriginalOrientToMovement=false,bOriginalDesiredRotation=false;
	void ChooseNextProduct();
	bool CollectPaidGoods();
	void ReturnUnsoldGoods();
	void ReleaseBucket();
	UFUNCTION() void OnRep_Phase();
	UFUNCTION() void OnRep_BucketState();
	TWeakObjectPtr<AActor> PresentedBucket;
	ECollisionEnabled::Type PresentedOriginalCollision=ECollisionEnabled::QueryAndPhysics;
	ECollisionResponse PresentedOriginalPawnResponse=ECR_Block;
};
