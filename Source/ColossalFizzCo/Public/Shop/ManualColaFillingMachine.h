#pragma once
#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "ManualColaFillingMachine.generated.h"
class UShopProductionStationComponent;
class UBottleFillingComponent;
class UStaticMeshComponent;
class UBoxComponent;
class USphereComponent;
class UResourceTransferEndpointComponent;
class UHoseTransferComponent;

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
	/** Changing this builds exactly this many query-only hose connection spheres. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="1",ClampMax="32")) int32 SupportedContentCount=4;
	/** Relative to PropMesh with HoseConnections at its default identity transform. Each entry has a viewport transform widget; arrow +X is nozzle forward. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(MakeEditWidget=true)) TArray<FTransform> HosePortTransforms;
	/** One entry per inlet. Zero accepts the attached source type; a positive value permits only that ContentID. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="0")) TArray<int32> HosePortContentIDs;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports",meta=(ClampMin="2",Units="cm")) float HosePortRadius=12.f;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Machine|Hose Ports") TEnumAsByte<ECollisionChannel> HosePortTraceChannel=ECC_Visibility;
	UPROPERTY(Transient,BlueprintReadOnly,Category="Machine|Hose Ports") TArray<TObjectPtr<USphereComponent>> HosePorts;
	UPROPERTY(Transient,BlueprintReadOnly,Category="Machine|Hose Ports") TArray<TObjectPtr<UResourceTransferEndpointComponent>> ReceivingHosePorts;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Machine|Hose Ports",meta=(ClampMin="0")) float MaximumIntakePressure=6.f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Machine|Hose Ports",meta=(ClampMin="0",DisplayName="Maximum Intake (portions/s)")) float MaximumIntakePortionsPerSecond=10.f;
	/** Type an empty receiving socket before attaching an external hose. No liquid is requested yet. */
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Machine|Hose Ports") bool PrepareReceivingHosePort(int32 PortIndex,int32 ContentID);
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") UResourceTransferEndpointComponent* GetReceivingHosePort(int32 PortIndex) const;
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") UHoseTransferComponent* GetSupplyHoseForPort(int32 PortIndex) const;
	int32 GetSupplyContentID(int32 PortIndex) const;
	bool IsSupplyInRange(int32 PortIndex) const;
	void ConfigureReceivingHosePort(UResourceTransferEndpointComponent* Port);
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") bool GetHosePortFromHit(const FHitResult& Hit,int32& PortIndex,FTransform& ConnectionTransform) const;
	UFUNCTION(BlueprintPure,Category="Machine|Hose Ports") FTransform GetHosePortTransform(int32 PortIndex) const;
	/** Call on the server after tracing a port. Records the source and snaps the nozzle's location AND orientation to the sphere. */
	UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Machine|Hose Ports") bool AttachSupplyNozzle(const FHitResult& Hit,USceneComponent* Nozzle,int32 SourceISID,AActor* Player);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool HasSupplyNozzle(int32 PortIndex,int32 SourceISID) const;
	void RestoreSupplyNozzles();
	void DisconnectSupplyNozzle(int32 PortIndex);
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pipe;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Lever;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SupplyPipes;
	UPROPERTY(Replicated) TArray<TObjectPtr<USceneComponent>> ConnectedNozzles;
	int32 BoundMachineISID=0;
	float DisplayedCapProgress=0.f;
	void RefreshHosePortLayout();
	UFUNCTION() void HandleSupplyReceived(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float ActualUnits,float IncomingPressure,float ActualRate);
	UFUNCTION() void HandleSupplySocketOverloaded(UResourceTransferEndpointComponent* Socket,UHoseTransferComponent* Hose,float IncomingPressure,float OfferedRate);
};
