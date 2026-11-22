#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "PropGrabInputComponent.generated.h"

class ACharacter;
class APropManager;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EPropGrabInputPhase : uint8 { Started, Ongoing, Triggered, Completed, Canceled };

UENUM(BlueprintType)
enum class EPropGrabIntent : uint8 { None, Grab, Drop, Place };

/** Local gesture recognition only. Actual interaction still uses the authoritative Prop Manager. */
struct COLOSSALFIZZCO_API FPropGrabGesture
{
	EPropGrabIntent Process(EPropGrabInputPhase Phase, double Now, uint64 HeldToken, double HoldSeconds, double DoubleTapSeconds);
private:
	bool bActive = false;
	bool bPerformed = false;
	double StartedAt = 0;
	uint64 StartedToken = 0;
	double LastTapAt = -1;
	uint64 LastTapToken = 0;
};

/** Shared G timing and server-side movement for actor pickups and placement. */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UPropGrabInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	UPropGrabInputComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0.01")) float HoldSeconds = .4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0.01")) float DoubleTapSeconds = .4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "1", Units = "cm/s")) float CarryTravelSpeed = 360.f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab") EPropGrabIntent LastIntent = EPropGrabIntent::None;
	/** Triggered/Completed/Canceled may all arrive on release; only the first ending consumes a gesture. */
	UFUNCTION(BlueprintCallable, Category = "Grab")
	void ProcessGrabInput(EPropGrabInputPhase Phase, AActor* HeldActor, UPrimitiveComponent* HeldComponent);
	/** Shared check for the line-traced component and the component pickup RPC. */
	UFUNCTION(BlueprintPure, Category = "Grab") bool CanGrabHitComponent(const UPrimitiveComponent* HittingComponent) const;
	/** Guards used by the existing owned-character RPC adapters before changing carry references. */
	UFUNCTION(BlueprintPure, Category = "Grab") bool CanGrabTarget(AActor* Target) const;
	UFUNCTION(BlueprintPure, Category = "Grab") bool OwnsHeldTarget(AActor* Target) const;
private:
	struct FActorTravel
	{
		TWeakObjectPtr<AActor> Actor;
		int32 ISID = 0;
		int64 Sequence = 0;
		bool bPlacing = false;
		bool bWasSimulating = false;
	};
	bool TryBeginActorTravel(EPropGrabInputPhase Phase, AActor* HeldActor);
	bool IsActorInAnotherPlayerHand(const AActor* Actor) const;
	void DropTravelActor();
	void CompleteTravelPickup(ACharacter& Player, AActor& Actor);
	UFUNCTION(Server, Reliable) void Server_BeginActorTravel(int64 Sequence, int32 ISID, AActor* Actor, FVector_NetQuantize10 HitLocation, bool bPlacing);
	UFUNCTION(Server, Reliable) void Server_EndActorTravel(int64 Sequence);
	FPropGrabGesture Gesture;
	FActorTravel Travel;
	int64 LocalTravelSequence = 0;
	int64 LastServerTravelSequence = 0;
	bool bTravelKeyHeld = false;
};
