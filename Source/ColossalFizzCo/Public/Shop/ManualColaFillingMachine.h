#pragma once
#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "ManualColaFillingMachine.generated.h"
class UShopProductionStationComponent;
class UBottleFillingComponent;
class UStaticMeshComponent;
class UBoxComponent;
class UResourceTransferEndpointComponent;
class UHoseSocketComponent;
class UHoseTransferComponent;
class AHoseCableActor;

/** Purchasable/carryable manual equipment using the existing managed-prop lifecycle. */
UCLASS()
class COLOSSALFIZZCO_API AManualColaFillingMachine : public AActivePropActor
{
	GENERATED_BODY()
public:
	AManualColaFillingMachine();
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanDemoteToISM() const override { return false; }
	virtual void OnApplySharedDefinitions(const FPropProductDefinition* Product,const FPropContentDefinition* Content) override;
	virtual void ResetTransientPoolState() override;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UShopProductionStationComponent> Production;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBottleFillingComponent> BottleFiller;
	/** Select these inherited components in the Blueprint viewport to move/rotate/resize their volumes. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<UBoxComponent> BottleInputVolume;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<UBoxComponent> CapInputVolume;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<UBoxComponent> SealInputVolume;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<UBoxComponent> SupplyInputVolume;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<USceneComponent> BottleHolderPoint;
	/** Bottle local +Z points toward the cap. At rest it slopes upward; during filling it slopes downward. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Cradle") TObjectPtr<USceneComponent> CradlePivot;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Cradle") TObjectPtr<UStaticMeshComponent> CradlePlatform;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Cradle") TObjectPtr<UStaticMeshComponent> CappingHead;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Cradle",meta=(ClampMin="0",Units="Degrees")) float CapDisplayTurns=720.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Cradle",meta=(Units="Degrees")) float RestCradlePitch=70.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Cradle",meta=(Units="Degrees")) float FillingCradlePitch=110.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Cradle",meta=(ClampMin="0.05",Units="s")) float CradleTravelSeconds=.6f;
	UPROPERTY(Replicated,VisibleAnywhere,BlueprintReadOnly,Category="Machine|Cradle") float CradleAlpha=0.f;
	UFUNCTION(BlueprintPure,Category="Machine|Cradle") bool IsCradleInFillPosition() const;
	void UpdateCradleMotion(float DeltaSeconds);
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<USceneComponent> SupplyDeliveryPoint;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<USceneComponent> HoseConnections;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Machine|Layout") TObjectPtr<UStaticMeshComponent> ContentSelectorDial;
	/** Number of active inlets on the content dial. Empty slots get a generated Hose Socket. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="1",ClampMax="32")) int32 SupportedContentCount=4;
	/** Layout for generated inlets, relative to HoseConnections. Local +X points out of the old mount and maps to the new socket's +Z. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(MakeEditWidget=true)) TArray<FTransform> HosePortTransforms;
	/** One entry per inlet. Zero accepts the attached source type; a positive value permits only that ContentID. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="0")) TArray<int32> HosePortContentIDs;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="2",Units="cm")) float HosePortRadius=12.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports") TEnumAsByte<ECollisionChannel> HosePortTraceChannel=ECC_Visibility;
	/** Authored children of HoseConnections are ordered by name, with generated sockets filling the remaining slots. */
	UPROPERTY(Transient,BlueprintReadOnly,Category="Machine|Hose Ports") TArray<TObjectPtr<UHoseSocketComponent>> HoseSockets;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Machine|Hose Ports",meta=(ClampMin="0")) float MaximumIntakePressure=6.f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Machine|Hose Ports",meta=(ClampMin="0",DisplayName="Maximum Intake (portions/s)")) float MaximumIntakePortionsPerSecond=10.f;
	/** Normal operating rate inherited by sockets without their own intake limit. Zero uses the available hose flow. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Machine|Hose Ports",meta=(ClampMin="0",DisplayName="Intake Flow Rate Limit (portions/s)")) float IntakeFlowPortionsPerSecond=0.f;
	/** Type an empty receiving socket before attaching an external hose. No liquid is requested yet. */
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Machine|Hose Ports") bool PrepareReceivingHosePort(int32 PortIndex,int32 ContentID);
	int32 GetRequiredHoseContentID(int32 PortIndex) const;
	/** A zero content ID reserves an unrestricted inlet for an empty hose while it waits for a source. */
	bool PrepareReceivingHoseConnection(int32 PortIndex, int32 ContentID, const AHoseCableActor* Hose);
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") UHoseSocketComponent* GetHoseSocket(int32 PortIndex) const;
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") UResourceTransferEndpointComponent* GetReceivingHosePort(int32 PortIndex) const;
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") UHoseTransferComponent* GetSupplyHoseForPort(int32 PortIndex) const;
	int32 GetSupplyContentID(int32 PortIndex) const;
	bool IsSupplyInRange(int32 PortIndex) const;
	virtual FVector GetSupplyConnectionOrigin() const { return GetActorLocation(); }
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") bool GetHosePortFromHit(const FHitResult& Hit,int32& PortIndex,FTransform& ConnectionTransform) const;
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") FTransform GetHosePortTransform(int32 PortIndex) const;
	/** Call on the server after tracing a port. Records the source and snaps the nozzle to the socket. */
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Machine|Hose Ports") bool AttachSupplyNozzle(const FHitResult& Hit,USceneComponent* Nozzle,int32 SourceISID,AActor* Player);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool HasSupplyNozzle(int32 PortIndex,int32 SourceISID) const;
	void RestoreSupplyNozzles();
	void DisconnectSupplyNozzle(int32 PortIndex);
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine|Layout") TObjectPtr<UStaticMeshComponent> Lever;
	virtual FTransform GetDefaultHosePortTransform(int32 Index) const;
	virtual void UpdateLeverVisual(float DeltaSeconds);
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pipe;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SupplyPipes;
	UPROPERTY(Replicated) TArray<TObjectPtr<USceneComponent>> ConnectedNozzles;
	FQuat ContentDialRestRotation = FQuat::Identity;
	int32 BoundMachineISID=0;
	float DisplayedCapProgress=0.f;
	void RefreshHosePortLayout();
	void RebuildHoseSockets();
	void ConfigureReceivingHosePort(UHoseSocketComponent& Port, bool bGenerated);
	UFUNCTION() void HandleSupplyReceived(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float ActualUnits,float IncomingPressure,float ActualRate);
	UFUNCTION() void HandleSupplySocketOverloaded(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float IncomingPressure,float OfferedRate);
};
