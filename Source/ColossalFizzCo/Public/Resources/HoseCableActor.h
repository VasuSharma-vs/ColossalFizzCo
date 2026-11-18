#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HoseCableActor.generated.h"

class UCollisionCableComponent;
class UHoseTransferComponent;
class UResourceTransferEndpointComponent;
class UStaticMeshComponent;

/** A reusable two-ended hose. Cable simulation, resource transport and connector art remain separate editable components. */
UCLASS(Blueprintable)
class COLOSSALFIZZCO_API AHoseCableActor : public AActor
{
    GENERATED_BODY()
public:
    AHoseCableActor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<UCollisionCableComponent> Cable;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Hose") TObjectPtr<UHoseTransferComponent> Transfer;
    /** Empty hoses can adopt the connected source's subtype; retained contents can never be relabeled. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hose|Connections") bool bMatchSourceResourceWhenEmpty=true;
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
    /** Bind effects here. Native rupture already stops flow, releases the stored payload and disconnects both ends. */
    UFUNCTION(BlueprintImplementableEvent,Category="Hose|Failure") void OnHoseRuptured(float Pressure,float ReleasedUnits);
private:
    UFUNCTION() void OnRep_Endpoints();
    UFUNCTION() void HandleBurst(float Pressure,float ReleasedUnits);
    void RefreshCableAttachments();
    void UpdateConnectorMeshes();
    bool CanAttach(UResourceTransferEndpointComponent* Port,bool bStart) const;
};
