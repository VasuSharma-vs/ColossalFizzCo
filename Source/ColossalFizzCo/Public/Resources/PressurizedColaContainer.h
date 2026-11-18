#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/ResourceEndpointProvider.h"
#include "PressurizedColaContainer.generated.h"

class UResourceTransferEndpointComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Hot bulk cola store. The outlet owns supply pressure/rate; the hose owns delivery and rupture. */
UCLASS(Blueprintable)
class COLOSSALFIZZCO_API APressurizedColaContainer : public AActor, public IResourceEndpointProvider
{
    GENERATED_BODY()
public:
    APressurizedColaContainer();
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual UResourceTransferEndpointComponent* GetResourceTransferEndpoint_Implementation() const override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container") TObjectPtr<USceneComponent> ContainerRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container") TObjectPtr<UResourceTransferEndpointComponent> SupplyPort;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container|Valve") TObjectPtr<USceneComponent> ValvePivot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container|Valve") TObjectPtr<USceneComponent> ValveRotor;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container|Valve") TObjectPtr<UStaticMeshComponent> ValveWheel;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cola Container|Display") TObjectPtr<UTextRenderComponent> SupplyDisplay;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Cola Container|Storage",meta=(ClampMin="0")) float InitialUnits=200.f;
    /** Gameplay pressure units, matching hose/socket ratings; not a real gas-law simulation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Replicated,Category="Cola Container|Supply",meta=(ClampMin="0")) float OutletPressure=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Replicated,Category="Cola Container|Supply",meta=(Units="Celsius")) float OutletTemperature=85.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Replicated,Category="Cola Container|Supply",meta=(ClampMin="0")) float MaxOutletUnitsPerSecond=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_ValveOpening,SaveGame,Category="Cola Container|Valve",meta=(ClampMin="0",ClampMax="1")) float ValveOpening=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Cola Container|Valve",meta=(ClampMin="1",Units="Degrees")) float ValveTravelDegrees=720.f;
    /** E turns toward open; Shift+E turns toward closed. Each press makes this many degrees of travel. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Cola Container|Valve",meta=(ClampMin="1",Units="Degrees")) float ValveInteractionStepDegrees=90.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Cola Container|Valve",meta=(ClampMin="1",Units="Degrees")) float ValveVisualDegreesPerSecond=180.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Replicated,Category="Cola Container|Monitor") float TotalDispensedUnits=0.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Replicated,Category="Cola Container|Monitor") float CurrentOutletUnitsPerSecond=0.f;
    UFUNCTION(BlueprintPure,Category="Cola Container") float GetRemainingUnits() const;
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Cola Container|Valve") bool SetValveOpening(float Opening);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Cola Container|Valve") bool RotateValve(float Degrees);
    UFUNCTION(BlueprintCallable,BlueprintAuthorityOnly,Category="Cola Container|Supply") void RefreshSupplySettings();
private:
    UFUNCTION() void OnRep_ValveOpening();
    void UpdateDisplay();
    float DisplayedValveAngle=0.f;
    float PreviousSuppliedUnits=0.f;
    float DisplayAccumulator=0.f;
};
