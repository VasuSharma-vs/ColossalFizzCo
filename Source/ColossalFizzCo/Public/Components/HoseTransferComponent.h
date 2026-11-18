#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/ResourceTransferTypes.h"
#include "HoseTransferComponent.generated.h"

class UResourceTransferEndpointComponent;
class APropManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHoseContentsReleased,EResourceTransportCategory,Category,int32,ResourceID,float,Units);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHoseBurst,float,Pressure,float,ReleasedUnits);

/** Server-authoritative flow through a finite hose inventory. Physical attachments drive Connect/Disconnect. */
UCLASS(ClassGroup=(Resources), meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UHoseTransferComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UHoseTransferComponent();
	virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer") EResourceTransportCategory Category=EResourceTransportCategory::Content;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer", meta=(ClampMin="1")) int32 ResourceID=1;
	/** Normal rating. Content uses portions/sec; fuel and electricity use their authored storage units/sec. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer", meta=(ClampMin="0")) float MaxUnitsPerSecond=5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") bool bTransferEnabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") bool bIntakeEnabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") bool bDrainEnabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer|Storage", meta=(ClampMin="0")) float BufferCapacityUnits=5.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Storage") FHoseResourceBuffer Buffer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive") bool bRequiresMotorDrive=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float RatedPressure=1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float FlowBoostPerPressure=1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float MaxDrivenUnitsPerSecond=50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BurstPressureThreshold=4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BurstDelay=.5f;
	/** A running source builds extra pressure when the finite hose is full and its receiver cannot take more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BlockedPressureRisePerSecond=1.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Drive") float CurrentPressure=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Drive") float CurrentFlowRate=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Failure") bool bBurst=false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Storage") float TotalReleasedUnits=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") TObjectPtr<UResourceTransferEndpointComponent> SourceEndpoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") TObjectPtr<UResourceTransferEndpointComponent> TargetEndpoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") float LastTransferredUnits=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") float LastIntakeUnits=0;
	/** Live receiver demand propagated to the supply valve; false never discards trapped contents. */
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Flow") bool IsRequestingContent() const;
	/** Bounded integration keeps continuous throughput independent of finite hose capacity and frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Transfer|Simulation", meta=(ClampMin="0.001",ClampMax="0.1")) float MaxTransferStepSeconds=.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Transfer|Simulation", meta=(ClampMin="1",ClampMax="1024")) int32 MaxTransferSubsteps=128;
	/** Time intentionally skipped after the substep budget is exhausted; resources are never moved for this time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resource Transfer|Simulation") double DroppedTransferTime=0;
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Failure") FHoseBurst OnBurst;
	/** Electricity releases dissipated energy, never spilled liquid. Managed content also enters ReleasedLiquids. */
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Storage") FHoseContentsReleased OnContentsReleased;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") bool ConnectEndpoints(UResourceTransferEndpointComponent* Source,UResourceTransferEndpointComponent* Target);
	/** Disconnect retains trapped contents. Disable intake and leave drain enabled to empty into the receiver. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") void DisconnectEndpoints();
	UFUNCTION(BlueprintPure, Category="Resource Transfer") bool HasValidConnection() const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") float TransferForDuration(float DeltaSeconds);
	/** Explicitly discard contents, reporting the exact released/dissipated amount once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Storage") float PurgeContents();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Failure") bool RepairHose();

private:
	friend class UResourceTransferEndpointComponent;
	bool CanConnect(const UResourceTransferEndpointComponent* Source,const UResourceTransferEndpointComponent* Target) const;
	bool HasValidBuffer() const;
	float IntegrateTransferStep(float DeltaSeconds);
	float ReleaseBufferedContents();
	void UnbindEndpoints();
	UPROPERTY(Replicated, SaveGame) TObjectPtr<APropManager> BufferManager;
	UPROPERTY(Replicated, SaveGame) bool bBufferUsesManagedContent=false;
	float OverpressureSeconds=0;
	bool bTransferInProgress=false;
};
