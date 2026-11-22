#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HoseCableActor.generated.h"

class UCollisionCableComponent;
class UHoseSocketComponent;
class UHoseTransferComponent;
class UResourceTransferEndpointComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
enum class EHoseDischargePoint : uint8;

USTRUCT()
struct FHoseHandGrip
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<USceneComponent> Component = nullptr;
    UPROPERTY() FName Socket = NAME_None;
};

/** A reusable two-ended hose. Cable simulation, resource transport and connector art remain separate editable components. */
UCLASS(Blueprintable)
class COLOSSALFIZZCO_API AHoseCableActor : public AActor
{
    GENERATED_BODY()
public:
    AHoseCableActor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<UCollisionCableComponent> Cable;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<UHoseTransferComponent> Transfer;
    /** Empty hoses can adopt the connected source's subtype; retained contents can never be relabeled. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Connections") bool bMatchSourceResourceWhenEmpty=true;
    /** Pickup along the hose brings the nearest reachable free connector to the player's hand. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Hose|Grabbing") bool bCarryConnectorOnPickup=false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hose|Grabbing", meta=(ClampMin="1", Units="cm/s"))
    float HandPickupSpeed = 360.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose|Connectors") TObjectPtr<UStaticMeshComponent> StartEndpointMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose|Connectors") TObjectPtr<UStaticMeshComponent> EndEndpointMesh;
    /** Offsets are relative to the cable end frame; connector local Z points out along the end tangent. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Connectors") FTransform StartMeshOffset=FTransform(FVector::ZeroVector);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Connectors") FTransform EndMeshOffset=FTransform(FVector::ZeroVector);
    UPROPERTY(EditInstanceOnly,Category="Hose|Connections",meta=(UseComponentPicker,AllowAnyActor,AllowedClasses="/Script/ColossalFizzCo.ResourceTransferEndpointComponent")) FComponentReference InitialStartPort;
    UPROPERTY(EditInstanceOnly,Category="Hose|Connections",meta=(UseComponentPicker,AllowAnyActor,AllowedClasses="/Script/ColossalFizzCo.ResourceTransferEndpointComponent")) FComponentReference InitialEndPort;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Endpoints,Category="Hose|Connections") TObjectPtr<UResourceTransferEndpointComponent> StartPort;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Endpoints,Category="Hose|Connections") TObjectPtr<UResourceTransferEndpointComponent> EndPort;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Hose|Connections") bool ConnectStart(UResourceTransferEndpointComponent* Port);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Hose|Connections") bool ConnectEnd(UResourceTransferEndpointComponent* Port);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Hose|Connections") void DisconnectStart();
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Hose|Connections") void DisconnectEnd();
    void EjectConnectorFrom(UResourceTransferEndpointComponent* Port,float Pressure);
    // Port roles are independent of the physical cable point selected by the player.
    int32 GetSupplyConnectorIndex() const;
    int32 GetReceivingConnectorIndex() const;
    bool ConnectSupplyAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex);
    bool ConnectReceivingAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex);
    UStaticMeshComponent* GetConnectorMesh(int32 PointIndex) const;
    bool GetDischargeTransform(EHoseDischargePoint Point,FTransform& Transform) const;
    bool CanGrabConnector(int32 PointIndex) const;
    bool UsesConnectorHandGrip() const;
    bool RequestConnectorHandGrip(int32 PointIndex, USceneComponent* Hand, FName Socket);
    int32 FindHandHeldConnector(const AActor* Holder) const;
    bool IsLooseHoseHeldBy(const AActor* Holder) const;
    int32 FindHeldConnector(const AActor* Holder) const;
    bool ReleaseHandHeldConnector(const AActor* Holder);
    /** Called by the Blueprint's Request Attach to Component implementation for the selected connector. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Hose|Grabbing")
    bool AttachSelectedConnectorToHand(USceneComponent* Hand, FName Socket);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Failure",meta=(DisplayName="Hose Discharge System")) TObjectPtr<UNiagaraSystem> RuptureSpray;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose|Failure") TObjectPtr<UNiagaraComponent> DischargeJet;
    /** How strongly a pressurized jet pushes a loose connector. Zero disables the recoil. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Discharge",meta=(ClampMin="0")) float JetReactionStrength=1.f;
    /** Content wall failures stop delivery and divert the supply to the tear with both connectors seated. */
    UFUNCTION(BlueprintImplementableEvent,Category="Hose|Failure") void OnHoseRuptured(float Pressure,float ReleasedUnits);
private:
    friend class UHoseSocketComponent;
    UPROPERTY(ReplicatedUsing=OnRep_Endpoints) bool bSourceAtEnd = false;
    UPROPERTY(ReplicatedUsing=OnRep_Endpoints) FHoseHandGrip StartHandGrip;
    UPROPERTY(ReplicatedUsing=OnRep_Endpoints) FHoseHandGrip EndHandGrip;
    TWeakObjectPtr<USceneComponent> StartGripTickSource;
    TWeakObjectPtr<USceneComponent> EndGripTickSource;
    int32 RequestedConnectorIndex = INDEX_NONE;
    UFUNCTION() void OnRep_Endpoints();
    UFUNCTION() void HandleBurst(float Pressure,float ReleasedUnits);
    bool bDischargeEmitting=false;
    int32 DischargeColorContentID=0;
    float JetReactionPhase=0.f;
    void RefreshCableAttachments(int32 ConnectorToSnap = INDEX_NONE);
    void UpdateConnectorMeshes();
    void UpdatePressureVisuals();
    void UpdateDischargeJet();
    void ApplyDischargeRecoil(float DeltaSeconds);
    bool CanAttach(UResourceTransferEndpointComponent* Port, bool bSupply, int32 ExpectedResourceID, bool bWaitForSource) const;
    bool ConnectPortAtConnector(UResourceTransferEndpointComponent* Port, int32 PointIndex, bool bSupply,
        bool bAdoptReceiverResource, bool bWaitForSource = false);
    void DisconnectPort(TObjectPtr<UResourceTransferEndpointComponent>& Port);
};
