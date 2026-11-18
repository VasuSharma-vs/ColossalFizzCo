#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/ResourceTransferTypes.h"
#include "ResourceTransferEndpointComponent.generated.h"

class APropManager;
class UHoseTransferComponent;
class UResourcePumpComponent;
class UResourceTransferEndpointComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FResourceSocketOverloaded,UResourceTransferEndpointComponent*,Socket,UHoseTransferComponent*,Hose,float,IncomingPressure,float,OfferedUnitsPerSecond);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FResourceReceived,UResourceTransferEndpointComponent*,Socket,UHoseTransferComponent*,Hose,float,ActualUnits,float,IncomingPressure,float,ActualUnitsPerSecond);

/** One typed connection port backed by either a local resource store or a canonical managed content instance. */
UCLASS(ClassGroup=(Resources), meta=(BlueprintSpawnableComponent))
class COLOSSALFIZZCO_API UResourceTransferEndpointComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UResourceTransferEndpointComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") EResourceTransportCategory Category=EResourceTransportCategory::Content;
	/** Content uses the manager's ContentID. Fuel and electricity IDs identify compatible fuels or electrical systems. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer", meta=(ClampMin="1")) int32 ResourceID=1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") bool bCanSupply=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer") bool bCanReceive=true;
	/** Zero permits shared connections. Physical sockets normally use one, including hoses attached at only one end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer", meta=(ClampMin="0")) int32 MaxConnectedHoses=0;
	UFUNCTION(BlueprintPure, Category="Resource Transfer") bool CanAcceptHose(const UHoseTransferComponent* Hose) const;
	/** Operational valve/demand gates; closing either leaves the hose physically connected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Flow") bool bSupplyEnabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Flow") bool bRequestsFlow=true;
	/** Zero preserves unrestricted supply. A positive value caps source intake independently of hose discharge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Flow", meta=(ClampMin="0")) float MaxSupplyUnitsPerSecond=0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Flow") float TotalSuppliedUnits=0.f;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Flow") bool SetSupplyEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Flow") bool SetFlowRequested(bool bRequested);
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Flow") bool IsSupplyRequested() const;
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Flow") UHoseTransferComponent* GetReceivingHose() const;
	/** Only explicit content adapters may bridge local storage and canonical managed bottles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Managed Content") bool bAllowManagedContentBridge=false;
	/** Receiver proxy fill stop; zero uses the bottle's physical capacity. This is a content-level fraction, not portions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Managed Content", meta=(ClampMin="0")) float MaximumManagedContentLevel=0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer|Storage") float ContentTemperature=20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer|Storage", meta=(ClampMin="0")) double ContentCreationTime=0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Socket") bool bEnableIntakeOverload=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Socket", meta=(ClampMin="0")) float MaxIntakePressure=6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Socket", meta=(ClampMin="0")) float MaxIntakeUnitsPerSecond=10.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Socket") float LastIncomingPressure=0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Resource Transfer|Socket") float LastIncomingUnitsPerSecond=0.f;
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Socket") FResourceSocketOverloaded OnSocketOverloaded;
	UPROPERTY(BlueprintAssignable, Category="Resource Transfer|Flow") FResourceReceived OnResourceReceived;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float PushPressure=0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float PullPressure=0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive") bool bMotorRunning=false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive", meta=(ClampMin="0")) float MotorPressureBoost=0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Resource Transfer|Drive") TObjectPtr<UResourcePumpComponent> ConnectedMotor;
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Drive") bool IsMotorRunning() const;
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Drive") float GetEffectivePushPressure() const;
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Drive") float GetEffectivePullPressure() const;
	/** Optional gameplay admission veto. Native type, direction, world and storage checks always still apply. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Resource Transfer") bool CanTransferResource(bool bAsSource) const;
	virtual bool CanTransferResource_Implementation(bool bAsSource) const;
	/** Local store capacity. Managed content uses its product's real capacity instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, SaveGame, Category="Resource Transfer|Storage", meta=(ClampMin="0")) float CapacityUnits=100.f;
	/** Initial local amount. Runtime writes use SetStoredUnits; managed endpoints never read this cache. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category="Resource Transfer|Storage", meta=(ClampMin="0")) float StoredUnits=0.f;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Storage") bool SetStoredUnits(float Units);
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Storage") float GetStoredUnits() const;
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Storage") float GetFreeCapacityUnits() const;
	UFUNCTION(BlueprintPure, Category="Resource Transfer") bool SupportsResource(EResourceTransportCategory InCategory,int32 InResourceID) const;
	/** Bind the port to an actual managed content container. The expected ID also constrains initially empty receivers. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Managed Content") bool ConfigureManagedContent(APropManager* Manager,int32 InstanceID,int32 ExpectedContentID);
	/** A machine socket receives directly into a managed bottle. Rebinding requires demand off; only parking at zero is allowed during a delivery callback. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Resource Transfer|Managed Content") bool ConfigureManagedReceivingProxy(APropManager* Manager,int32 TargetInstanceID,int32 ExpectedContentID);
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Managed Content") bool IsManagedReceivingProxy() const { return bManagedReceivingProxy; }
	/** Pool lifecycle cleanup: disconnect physical/logical hoses and invalidate this instance binding without losing trapped hose contents. */
	void ClearManagedContentBinding();
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Managed Content") bool IsManagedContentEndpoint() const { return bManagedContent; }
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Managed Content") APropManager* GetManagedContentManager() const { return ContentManager; }
	UFUNCTION(BlueprintPure, Category="Resource Transfer|Managed Content") int32 GetManagedContentISID() const { return ContentISID; }

private:
	friend class UHoseTransferComponent;
	bool HasValidStore() const;
	bool IsIntakeOverloaded(float Pressure,float UnitsPerSecond) const;
	void NotifyIntakeOverload(UHoseTransferComponent* Hose,float Pressure,float UnitsPerSecond);
	void NotifyResourceReceived(UHoseTransferComponent* Hose,float Units,float Pressure,float UnitsPerSecond);
	UPROPERTY(Replicated) bool bManagedContent=false;
	UPROPERTY(Replicated) bool bManagedReceivingProxy=false;
	UPROPERTY(Replicated) TObjectPtr<APropManager> ContentManager;
	UPROPERTY(Replicated) int32 ContentISID=0;
	TArray<TWeakObjectPtr<UHoseTransferComponent>> ConnectedHoses;
};
