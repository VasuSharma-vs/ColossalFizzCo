#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "ResourceReservoirActor.generated.h"
class UStaticMeshComponent;
class UResourceTransferEndpointComponent;
class UResourcePumpComponent;
class UCablePullResponseComponent;

/** Editable resource tank/socket example; other actors can add the same endpoint and motor components. */
UCLASS(Blueprintable)
class COLOSSALFIZZCO_API AResourceReservoirActor : public AActor, public IResourceEndpointProvider
{
    GENERATED_BODY()
public:
    AResourceReservoirActor();
    virtual UResourceTransferEndpointComponent* GetResourceTransferEndpoint_Implementation() const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Resource") TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Resource") TObjectPtr<UResourceTransferEndpointComponent> ResourcePort;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Resource") TObjectPtr<UResourcePumpComponent> PumpMotor;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Resource") TObjectPtr<UCablePullResponseComponent> PullResponse;
};
