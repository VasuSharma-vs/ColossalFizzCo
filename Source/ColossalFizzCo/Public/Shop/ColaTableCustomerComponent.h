#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ColaTableCustomerComponent.generated.h"

class AAIController;
class AActivePropActor;
class ACharacter;
class APropManager;
class UAnimInstance;
class UPropContentDataAsset;
class UStaticMeshComponent;
class UWidgetComponent;
struct FShopProductScanData;

namespace ColaCustomerThrow
{
	inline constexpr double WindupEnd = .40;
	inline constexpr double ReleaseAt = WindupEnd + .36;
	inline constexpr double FollowThroughEnd = ReleaseAt + .20;
	inline constexpr double RecoveryEnd = FollowThroughEnd + .38;
}

UCLASS()
class COLOSSALFIZZCO_API AColaBottleBin : public AActor
{
	GENERATED_BODY()

public:
	AColaBottleBin();
	FVector GetOpeningLocation() const;
	bool CrossedOpening(FVector Previous, FVector Current) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bin")
	TObjectPtr<UStaticMeshComponent> BinMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bin")
	TObjectPtr<USceneComponent> Opening;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bin", meta = (ClampMin = "1", Units = "cm"))
	float OpeningRadius = 34.f;
};

UENUM(BlueprintType)
enum class EColaTableCustomerState : uint8
{
	ApproachingTable,
	WaitingAtTable,
	Drinking,
	ApproachingBin,
	ThrowingBottle,
	PreparingThrow
};

/** A table visit for a placed AI character. The server owns the bottle, purchase, sips and disposal. */
UCLASS(ClassGroup = (Shop), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UColaTableCustomerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UColaTableCustomerComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void GetSipPoseWeights(float& OutLift, float& OutHeadTilt) const;
	double GetThrowElapsed() const;
	const FTransform& GetBottleMouthInHand() const { return BottleMouthInHand; }

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<AActor> ServingTable;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<AColaBottleBin> BottleBin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer")
	TObjectPtr<UPropContentDataAsset> RequestedCola;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer")
	FName TabletopMeshName = TEXT("SM_Cube");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer")
	FName BottleGripSocket = TEXT("HandGrip_R");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer", meta = (ClampMin = "0", Units = "s"))
	float TableWaitSeconds = 2.5f;

	/** Pause after lowering the bottle before starting the next sip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer|Drinking", meta = (ClampMin = "0.8", Units = "s"))
	float SecondsBetweenSips = 3.f;

	/** Number of this product's portions consumed by one sip. Fractions are supported. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer|Drinking", meta = (ClampMin = "0.05", UIMax = "4"))
	float PortionsPerSip = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer|Drinking")
	TSoftClassPtr<UAnimInstance> SipAnimationLayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer", meta = (ClampMin = "1", Units = "cm/s"))
	float BottleCarryWalkSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Customer", meta = (ClampMin = "100", Units = "cm"))
	float WanderRadius = 1400.f;

	UPROPERTY(ReplicatedUsing = OnRep_VisitState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Customer")
	EColaTableCustomerState VisitState = EColaTableCustomerState::ApproachingTable;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Customer")
	int32 BottleISID = 0;

private:
	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> RequestLabel;

	UPROPERTY(Replicated)
	double SipStartedAt = -1.0;

	UPROPERTY(Replicated)
	double ThrowStartedAt = -1.0;

	UPROPERTY(Replicated)
	FTransform BottleReleasePose = FTransform::Identity;

	UPROPERTY(Replicated)
	FVector BottleReleaseVelocity = FVector::ZeroVector;

	/** The bottle's neck position and upright orientation relative to hand_r. */
	UPROPERTY(Replicated)
	FTransform BottleMouthInHand = FTransform::Identity;

	UPROPERTY(Replicated)
	FTransform BottleGripInSocket = FTransform::Identity;

	TWeakObjectPtr<ACharacter> Customer;
	TWeakObjectPtr<AActivePropActor> ClientGrippedBottle;
	TWeakObjectPtr<UStaticMeshComponent> Tabletop;
	TWeakObjectPtr<AAIController> Controller;
	FName GripSocket;
	FVector ApproachTarget = FVector::ZeroVector;
	FVector LastBottleLocation = FVector::ZeroVector;
	bool bHasApproachTarget = false;
	bool bSipTaken = false;
	TOptional<float> WalkSpeedBeforeBottle;
	double NextActionTime = 0.0;

	void CreateRequestLabel();
	UFUNCTION()
	void OnRep_VisitState();

	double GetAnimationElapsed(double StartedAt) const;
	void ChangeState(EColaTableCustomerState NextState, double Now);
	void SetBottleCarryMovement(bool bCarrying);
	void UpdateClientBottleGrip();
	void ClearClientBottleGrip();
	void SyncClientBottleRelease();
	bool Approach(AAIController& AI, const FBox& Bounds, double Now, const FVector* BottleLocation = nullptr);
	void WalkAround(AAIController& AI, double Now);
	int32 FindBottle(const APropManager& Manager, FShopProductScanData& OutQuote) const;
	bool BuyBottle(APropManager& Manager, const FShopProductScanData& Quote);
	void Drink(APropManager& Manager, AAIController& AI, double Now);
	void ThrowBottle(APropManager& Manager, double Now);
	void TrackThrownBottle(APropManager& Manager, double Now);
	void Face(FVector Target) const;
};
