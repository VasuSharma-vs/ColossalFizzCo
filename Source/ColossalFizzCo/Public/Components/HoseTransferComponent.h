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
	/** Trickle rate at a loose content connector after the trapped pressure is gone. A positive rate lets the hose drain dry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Transfer|Flow", meta=(ClampMin="0.01")) float UnpressurizedDrainUnitsPerSecond=1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive") bool bRequiresMotorDrive=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float RatedPressure=1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float FlowBoostPerPressure=1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float MaxDrivenUnitsPerSecond=50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BurstPressureThreshold=4.f;
	/** Maximum vacuum magnitude on a content hose. At this much suction the hose collapses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float VacuumCollapseThreshold=4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BurstDelay=.5f;
	/** Extra pressure rise in a blocked, full line. Content hoses build this extra pressure only between two pushing ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Failure", meta=(ClampMin="0")) float BlockedPressureRisePerSecond=1.f;
	/** Signed pressure inside the hose. Positive inflates, negative collapses. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Drive") float CurrentPressure=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Drive") float CurrentFlowRate=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Failure") bool bBurst=false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Failure") EHoseFailureMode FailureMode=EHoseFailureMode::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Storage") float TotalReleasedUnits=0;
	/** Actual liquid leaving through an open tip or tear, sampled over the last server update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Flow") FHoseDischargeState Discharge;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") TObjectPtr<UResourceTransferEndpointComponent> SourceEndpoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") TObjectPtr<UResourceTransferEndpointComponent> TargetEndpoint;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") float LastTransferredUnits=0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer") float LastIntakeUnits=0;
	/** Signed amount at this port in the last update. Positive enters the hose and negative exits it. */
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Flow") float GetLastPortFlowUnits(const UResourceTransferEndpointComponent* Port) const;
	/** Sample duration used to convert the actual amounts into units per second. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Flow") float LastTransferUpdateSeconds=0;
	/** Live receiver demand propagated to the supply valve; false never discards trapped contents. */
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Flow") bool IsRequestingContent() const;
	/** Available input rate at a neutral content receiver, before storage or downstream demand limits it. */
	float GetContentFeedRate(const UResourceTransferEndpointComponent& Receiver) const;
	/** Bounded integration keeps continuous throughput independent of finite hose capacity and frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Transfer|Simulation", meta=(ClampMin="0.001",ClampMax="0.1")) float MaxTransferStepSeconds=.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource Transfer|Simulation", meta=(ClampMin="1",ClampMax="1024")) int32 MaxTransferSubsteps=128;
	/** Time intentionally skipped after the substep budget is exhausted; resources are never moved for this time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resource Transfer|Simulation") double DroppedTransferTime=0;
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Failure") FHoseBurst OnBurst;
	/** Electricity releases dissipated energy, never spilled liquid. Managed content also enters ReleasedLiquids. */
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Storage") FHoseContentsReleased OnContentsReleased;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") bool ConnectEndpoints(UResourceTransferEndpointComponent* Source,UResourceTransferEndpointComponent* Target);
	/** Clears the logical connection. Unplug the physical connector to vent a content hose. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") void DisconnectEndpoints();
	UFUNCTION(BlueprintPure, Category="Resource Transfer") bool HasValidConnection() const;
	/** The socket at the other end of the connected pair, independent of flow direction. */
	UFUNCTION(BlueprintPure, Category="Resource Transfer") UResourceTransferEndpointComponent* GetOtherEndpoint(const UResourceTransferEndpointComponent* Port) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer") float TransferForDuration(float DeltaSeconds);
	/** Explicitly discard contents, reporting the exact released/dissipated amount once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Storage") float PurgeContents();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Failure") bool RepairHose();

private:
	friend class UResourceTransferEndpointComponent;
	friend class AHoseCableActor;
	bool CanConnect(const UResourceTransferEndpointComponent* Source,const UResourceTransferEndpointComponent* Target) const;
	bool IsRequestingContentFrom(const UResourceTransferEndpointComponent* Port) const;
	bool HasValidBuffer() const;
	bool GetContentEndpoints(UResourceTransferEndpointComponent*& First,UResourceTransferEndpointComponent*& Second) const;
	void CaptureUnpluggedPressure(EHoseDischargePoint Point);
	EHoseDischargePoint FindOpenTip() const;
	float DrainTrappedContents(EHoseDischargePoint Point,float DeltaSeconds,float SupplyPressure);
	float IntegrateTransferStep(float DeltaSeconds);
	float IntegrateContentStep(float DeltaSeconds);
	float GetPressureFlowCapacity(float Head) const;
	float GetContentLineRate(float Head,bool bPassive,float IntakeAssist) const;
	float IntakeFrom(UResourceTransferEndpointComponent* Port,float Units);
	float DeliverTo(UResourceTransferEndpointComponent* Port,float Units,float Pressure,float DeltaSeconds);
	void Rupture(EHoseFailureMode Mode);
	float DischargeContents(EHoseDischargePoint Point,float Units,float Pressure);
	float ReleaseContents(float Units,FGuid Episode,const FVector& Location,bool bContinueEpisode);
	float ReleaseBufferedContents();
	void UnbindEndpoints();
	UPROPERTY(Replicated, SaveGame) TObjectPtr<APropManager> BufferManager;
	UPROPERTY(Replicated, SaveGame) bool bBufferUsesManagedContent=false;
	UPROPERTY(Replicated) float LastStartPortFlowUnits=0.f;
	UPROPERTY(Replicated) float LastEndPortFlowUnits=0.f;
	TMap<FIntVector,FGuid> DischargeEpisodes;
	EHoseDischargePoint EpisodePoint=EHoseDischargePoint::None;
	EHoseDischargePoint LastUnpluggedTip=EHoseDischargePoint::None;
	float TrappedDrainPressure=0.f;
	float DischargedDuringUpdate=0.f;
	EHoseFailureMode PendingBodyFailure=EHoseFailureMode::None;
	float BackPressure=0.f;
	float OverpressureSeconds=0;
	bool bTransferInProgress=false;
};
