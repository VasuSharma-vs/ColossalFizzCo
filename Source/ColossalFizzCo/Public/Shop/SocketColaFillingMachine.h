#pragma once

#include "Shop/ManualColaFillingMachine.h"
#include "SocketColaFillingMachine.generated.h"

class UArrowComponent;
class UCollisionCableComponent;
class UPrimitiveComponent;

USTRUCT()
struct FSocketMachineInputPlacement
{
	GENERATED_BODY()

	UPROPERTY() int32 ISID = 0;
	UPROPERTY() FTransform Destination;

	TWeakObjectPtr<AActivePropActor> Item;
	TWeakObjectPtr<UBoxComponent> Socket;
};

USTRUCT()
struct FSocketMachinePlacementItem
{
	GENERATED_BODY()

	UPROPERTY() int32 ISID = 0;
	UPROPERTY() bool bWasHidden = false;
	UPROPERTY() bool bHadCollision = false;
};

/** Manual station with a bottle cradle, cap magazine and separate lever controller. */
UCLASS()
class COLOSSALFIZZCO_API ASocketColaFillingMachine : public AManualColaFillingMachine
{
	GENERATED_BODY()

public:
	ASocketColaFillingMachine();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void ResetTransientPoolState() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FVector GetSupplyConnectionOrigin() const override;
	virtual bool AllowsPlayerGrab() const override { return false; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UStaticMeshComponent> FillingController;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UStaticMeshComponent> StandRotator;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UStaticMeshComponent> CapPin;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UArrowComponent> BottleSocket;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UArrowComponent> CapSocket;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout")
	TObjectPtr<UCollisionCableComponent> ControlCable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Machine|Cap Magazine", meta=(ClampMin="1", Units="cm"))
	float CapStackSpacing = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Machine|Cap Magazine")
	TArray<EBottleCapType> SupportedCapTypes = {EBottleCapType::Twist};
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Machine|Layout", meta=(ClampMin="0.05", Units="s"))
	float LeverTravelSeconds = .75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Machine|Layout", meta=(ClampMin="1", Units="cm/s"))
	float InputPlacementSpeed = 360.f;

	bool IsSocketInteractionComponent(const UPrimitiveComponent* Component, bool bIncludeLever = true) const;
	bool CanReachControl(const APawn* Player, const UPrimitiveComponent* Control) const;
	bool SupportsCapType(EBottleCapType Type) const { return SupportedCapTypes.Contains(Type); }
	APawn* GetPlacementOperator() const { return PlacementOperator.Get(); }
	bool IsMachinePlacementActive() const { return bMachinePlacementActive; }
	bool BeginMachinePlacement(APawn& Player);
	bool FinishMachinePlacement(APawn& Player, const FTransform& Pose);
	void CancelMachinePlacement(APawn& Player);
	UFUNCTION(BlueprintCallable, Category="Machine|World Interaction")
	void BeginSocketInteraction(APawn* Player, UPrimitiveComponent* HittingComponent);
	UFUNCTION(BlueprintCallable, Category="Machine|World Interaction")
	void EndSocketInteraction(APawn* Player);

protected:
	virtual FTransform GetDefaultHosePortTransform(int32 Index) const override;
	virtual void UpdateLeverVisual(float DeltaSeconds) override;
	virtual void UpdateControlLayout() override;

private:
	friend class UShopProductionStationComponent;

	UPROPERTY(Replicated)
	bool bWorldLeverPressed = false;
	TWeakObjectPtr<APawn> WorldLeverOperator;
	UPROPERTY(Replicated)
	TArray<FSocketMachineInputPlacement> InputPlacements;
	UPROPERTY(ReplicatedUsing=OnRep_PlacementOperator)
	TObjectPtr<APawn> PlacementOperator;
	UPROPERTY(ReplicatedUsing=OnRep_PlacementOperator)
	bool bMachinePlacementActive = false;
	UPROPERTY(ReplicatedUsing=OnRep_PlacementOperator)
	bool bPlacementHadCollision = true;
	UPROPERTY(ReplicatedUsing=OnRep_PlacementOperator)
	TArray<FSocketMachinePlacementItem> PlacementItems;
	bool bPlacementWasHidden = false;
	int32 PlacementBottleISID = 0;
	FTransform PlacementBottleRelativeTransform;
	struct FAppliedPlacementItem
	{
		TWeakObjectPtr<AActivePropActor> Actor;
		FSocketMachinePlacementItem State;
	};
	TArray<FAppliedPlacementItem> AppliedPlacementItems;
	UFUNCTION() void OnRep_PlacementOperator();
	void UpdateMachinePlacement();
	void RestoreMachinePlacement();
	void RestorePlacementItems();
	void ApplyPlacementItems();
	void ReleaseWorldLever();
	void LoadHeldSupply(APawn* Player, UPrimitiveComponent* Socket);
	bool BeginInputPlacement(APropManager& Manager, int32 ID, UBoxComponent& Socket);
	int32 GetPendingInputCount(const UBoxComponent* Socket) const;
	bool IsBottleHolderReserved(int32 OtherThanISID) const;
	FTransform GetInputPlacementDestination(const AActivePropActor& Item, const UBoxComponent& Socket, int32 Slot) const;
	void UpdateInputPlacements(float DeltaSeconds);
	void UpdateClientInputPlacements(float DeltaSeconds);
	void CancelInputPlacements();
	UFUNCTION()
	void ResetControlCable(AActivePropActor* Prop, int32 InstanceID, int32 DefinitionID, FTransform Transform);
};
