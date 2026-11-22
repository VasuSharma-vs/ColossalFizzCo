#pragma once

#include "Components/ResourceTransferEndpointComponent.h"
#include "HoseSocketComponent.generated.h"

class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class UHoseSocketComponent;
class AManualColaFillingMachine;

UENUM(BlueprintType)
enum class EHoseSocketBreakReason : uint8
{
	IntakeOverload,
	StorageLimit,
	OuttakeOverload
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHoseSocketBroken, UHoseSocketComponent*, Socket,
	EHoseSocketBreakReason, Reason, UHoseTransferComponent*, Hose);

/** A single hose connection with editable trace bounds and an editor arrow pointing out of the mount. */
UCLASS(ClassGroup=(Resources), meta=(BlueprintSpawnableComponent, DisplayName="Hose Socket"))
class COLOSSALFIZZCO_API UHoseSocketComponent : public UResourceTransferEndpointComponent
{
	GENERATED_BODY()

public:
	UHoseSocketComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanAcceptHose(const UHoseTransferComponent* Hose) const override;
	virtual float GetFreeCapacityUnits() const override;

	/** Half-size of the Visibility trace box in socket-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hose Socket|Layout", meta=(ClampMin="1", Units="cm"))
	FVector TraceBoxExtent = FVector(22.0);
	/** Move the trace box clear of nearby art while keeping the hose at the component's pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hose Socket|Layout", meta=(Units="cm"))
	FVector TraceBoxOffset = FVector::ZeroVector;
	/** Break on reaching this amount of stored resource. Zero disables this limit. Set it at or below Capacity Units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Hose Socket|Limits", meta=(ClampMin="0"))
	float BreakAtStoredUnits = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Replicated, Category="Hose Socket|Status")
	TObjectPtr<AHoseCableActor> ConnectedHose;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Hose Socket|Status")
	bool bIsBroken = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="Hose Socket|Status")
	double TotalReceivedUnits = 0.0;
	UPROPERTY(BlueprintAssignable, Category="Hose Socket|Status")
	FHoseSocketBroken OnSocketBroken;

	UFUNCTION(BlueprintPure, Category="Hose Socket|Layout")
	UBoxComponent* GetTraceVolume() const;
	UFUNCTION(BlueprintPure, Category="Hose Socket|Layout")
	static UHoseSocketComponent* GetSocketFromHitComponent(const UPrimitiveComponent* HitComponent);

	/** The default index selects the usual connector for this port. Pass 0 or the last cable point to mount a specific end. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hose Socket")
	bool AttachHose(AHoseCableActor* Hose, int32 ConnectorIndex = -1);
	AManualColaFillingMachine* FindFillingMachine(int32& PortIndex) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hose Socket")
	bool DetachHose();
	/** Drain below the storage break limit before repairing. The socket starts accepting connections again. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hose Socket")
	bool RepairSocket();

	/** Units actually received (or supplied by this outlet) in the last hose update. */
	UFUNCTION(BlueprintPure, Category="Hose Socket|Status")
	float GetLastFlowUnits() const;
	UFUNCTION(BlueprintPure, Category="Hose Socket|Status")
	float GetCurrentFlowUnitsPerSecond() const;
	/** Positive leaves this socket for the hose. Negative enters this socket from the hose. */
	UFUNCTION(BlueprintPure, Category="Hose Socket|Status")
	float GetSignedFlowUnitsPerSecond() const;
	UFUNCTION(BlueprintPure, Category="Hose Socket|Status")
	bool IsHoseSupplying() const { return GetLastFlowUnits()>0.f; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnPhysicalHoseAttachmentChanged(AHoseCableActor* Hose, bool bAttached) override;
	virtual void NotifyIntakeOverload(UHoseTransferComponent* Hose, float Pressure, float UnitsPerSecond) override;
	virtual void NotifyOuttakeOverload(UHoseTransferComponent* Hose, float Pressure, float UnitsPerSecond) override;
	virtual void NotifyResourceReceived(UHoseTransferComponent* Hose, float Units, float Pressure, float UnitsPerSecond) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	UPROPERTY(Transient, DuplicateTransient) TObjectPtr<UBoxComponent> TraceVolume;
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, DuplicateTransient) TObjectPtr<UArrowComponent> DirectionArrow;
#endif
	void UpdateTraceVolume();
	float GetLastSignedFlowUnits() const;
	bool HasStorageBreakLimit() const;
	bool HasReachedStorageBreakLimit() const;
};
