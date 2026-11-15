#pragma once
#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "ManualColaFillingMachine.generated.h"
class UShopProductionStationComponent;
class UBottleFillingComponent;
class UStaticMeshComponent;

/** Purchasable/carryable manual equipment using the existing managed-prop lifecycle. */
UCLASS()
class COLOSSALFIZZCO_API AManualColaFillingMachine : public AActivePropActor
{
	GENERATED_BODY()
public:
	AManualColaFillingMachine();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanDemoteToISM() const override { return false; }
	virtual void OnApplySharedDefinitions(const FPropProductDefinition* Product,const FPropContentDefinition* Content) override;
	virtual void ResetTransientPoolState() override;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UShopProductionStationComponent> Production;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UBottleFillingComponent> BottleFiller;
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Pipe;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Lever;
	int32 BoundMachineISID=0;
};
