#pragma once
#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "ColaSupplyContainer.generated.h"

class UCollisionCableComponent;
class UStaticMeshComponent;
class UResourceTransferEndpointComponent;
class UCablePullResponseComponent;
class UHoseTransferComponent;

/** Managed bulk liquid container with a physical, grabbable hose endpoint. */
UCLASS()
class COLOSSALFIZZCO_API AColaSupplyContainer : public AActivePropActor, public IResourceEndpointProvider
{
	GENERATED_BODY()
public:
	AColaSupplyContainer();
	virtual bool CanDemoteToISM() const override { return false; }
	virtual void Tick(float DeltaSeconds) override;
	virtual void ResetTransientPoolState() override;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UCollisionCableComponent> SupplyHose;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UStaticMeshComponent> SupplyNozzle;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UStaticMeshComponent> SupplyConnector;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UResourceTransferEndpointComponent> SupplyPort;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UHoseTransferComponent> SupplyTransfer;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Supply Hose") TObjectPtr<UCablePullResponseComponent> CablePullResponse;
	virtual UResourceTransferEndpointComponent* GetResourceTransferEndpoint_Implementation() const override;
	bool MountHose(USceneComponent* Port);
	void ReleaseHose();
private:
	UFUNCTION() void PrepareDeliveredHose(AActivePropActor* Prop,int32 InstanceID,int32 DefinitionID,FTransform Transform);
};
