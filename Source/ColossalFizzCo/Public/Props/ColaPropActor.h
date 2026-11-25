#pragma once

#include "CoreMinimal.h"
#include "Props/InteractableProp.h"
#include "Components/RocketProjectileComponent.h"
#include "Props/ColaPricingLibrary.h"
#include "Props/ColaLiquidMotion.h"
#include "ColaPropActor.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class URocketProjectileComponent;
class UShopProductionStationComponent;
enum class EFillingMachineUpgradeType : uint8;

USTRUCT(BlueprintType)
struct FColaRocketLaunchConfiguration
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Seed = 0;
	UPROPERTY(BlueprintReadOnly) float Distance = 0;
	UPROPERTY(BlueprintReadOnly) float Speed = 0;
	UPROPERTY(BlueprintReadOnly) float Acceleration = 0;
	UPROPERTY(BlueprintReadOnly) FVector Direction = FVector::UpVector;
};

UENUM(BlueprintType)
enum class ECapRemovalOperation : uint8
{
	EjectPhysics UMETA(DisplayName = "Eject with Physics Impulse"),
	DropQuietly  UMETA(DisplayName = "Drop Quietly")
};

/**
 * Native C++ Base Class for Cola Bottles (BP_Cola, BP_SpicyCola, BP_SugerCrushCola).
 * Handles portion tracking, liquid material parameter driving, foam pressure & agitation physics,
 * portion-based sale quotes, cap ejection, and erratic rocket launches.
 */
UCLASS(Blueprintable, ClassGroup = (Props))
class COLOSSALFIZZCO_API AColaPropActor : public AActivePropActor
{
	GENERATED_BODY()

public:
	AColaPropActor();
	virtual void OnApplySharedDefinitions(const FPropProductDefinition* ProductDef, const FPropContentDefinition* ContentDef) override;
	virtual void ApplyUnsettlingImpulse(const FVector& Impulse) override;
	/** Fallback for bottle definitions with invalid empty mass. Never changes mesh scale. */
	UPROPERTY(EditAnywhere, Category="Cola|Physics", meta=(ClampMin="0.001", Units="kg")) float DefaultBottleMassKg = .05f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Physics") float BottleMass = .05f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Physics") float ContentMass = 0;
	void UpdateBottleMass();
	/** Impact agitation was tuned against a 5 kg body; preserve that response when mass changes. */
	UPROPERTY(EditAnywhere, Category="Cola|Physics", meta=(ClampMin="0.1", Units="kg")) float ImpactReferenceMassKg = 5.f;
	FVector NormalizePhysicalContactImpulse(const FVector& Impulse) const;
	FColaLiquidMotion LiquidMotion;
	void AdvanceLiquidVisuals(float DeltaSeconds, const FTransform& Transform);
	virtual bool CanDemoteToISM() const override;
	UFUNCTION(BlueprintPure, Category="Cola|Economy") FColaPriceQuote GetSaleQuote() const;
	virtual void ResetTransientPoolState() override;
	virtual void ResetForInstanceBinding() override;
	virtual void OnApplyEventData(const FPropEventUpdateData& Data, uint8 DirtyMask = 0xFF) override;
	virtual void OnApplyContinuousData(const FPropContinuousUpdateData& Data, float NewStrength=1, bool bStrengthChanged=false) override;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure", meta=(ClampMin="0", ClampMax="1")) float PressureMorphScale = .65f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure", meta=(ClampMin="0.1")) float PressureMorphInterpolationSpeed = 6.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure") float SmoothedPressureExpansion = 0;
	virtual void ApplyInitialImpactFromPromotion(FVector Location, const FVector& Impulse, AActor* InstigatorActor, int32 EventID = 0) override;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Content") bool SetBottleContentType(int32 NewContentID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Liquid") float FillBottleAtRate(float Rate, float DeltaSeconds);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Content") void RemoveBottleContent();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Failure") bool SubmitFailureRequest(const FContainmentFailureRequest& Request);
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Failure") EContainmentFailureType AcceptedFailureType = EContainmentFailureType::None;
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure") float InteractionPressure = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Pressure") float PressureDecayRate = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Pressure") float AtmosphereVentRate = 4.0f;
	FTimerHandle CapDebrisTimer;
	UPROPERTY(EditAnywhere, Category="Cola|Debug") bool bDebugBottleImpacts = false;
	TSet<int32> ProcessedImpactIDs;
	double LastImpactTime = -1;
	void ApplyBottleImpact(const FVector& Impulse, int32 EventID);
	bool UsesPlasticPressure() const;
	float GetBottlePressure() const;
	float GetBottlePressureTolerance() const;
	void AddAgitationPressure(float Amount);
	void UpdatePlasticPressure(float DeltaSeconds);
	bool StartPlasticPressureRelease(bool bTemporarySealFailure=false);
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing|Seal") bool HasTemporaryManufacturingSeal() const;
	/** Bottle-owned manufacturing rules. The machine supplies input and presentation only. */
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing") bool CanAcceptManufacturingContent(int32 ContentID) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing") void OnManufacturingLiquidReceived(float PreviousLevel);
	/** Hose delivery supplies line pressure separately from the canonical liquid/temperature transaction. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing") void OnManufacturingSupplyReceived(float ActualUnits,float IncomingPressure,float ActualRate);
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing") bool IsManufacturingFlowActive() const;
	float GetActiveFillingUpgradeValue(EFillingMachineUpgradeType Type) const;
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing|Seal") bool CanApplyManufacturingSeal() const;
	bool BuildManufacturingSealDelta(FPropEventSyncDelta& OutDelta, float DurationSeconds = 0.0f) const;
	/** Applies the bottle state; stations use BuildManufacturingSealDelta for atomic consumption of a physical seal. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Seal") bool AddManufacturingSeal();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Seal") void NotifyManufacturingLeverReleased();
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing|Seal") float GetManufacturingSealSecondsRemaining() const;
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing|Seal") float GetManufacturingSealIntegrity() const;
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing") bool IsManufacturingSafeToRelease() const;
	UFUNCTION(BlueprintPure, Category="Cola|Manufacturing|Cap") bool CanManufacturingCap() const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Cap") bool AttachManufacturingCap(int32 CapISID);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Cap") bool AdvanceManufacturingCap(float ProgressDelta, bool bSufficientHold);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Cap") bool CloseManufacturingCap(float Torque);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Cap") void UpdateManufacturingCapLeakage(float DeltaSeconds, bool bSufficientHold, FGuid ReleaseEpisode);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cola|Manufacturing") bool EvaluateManufacturingPressureFailure(bool bOverfill=false);
	/** Override in each cola Blueprint to choose its failure reaction; normal plastic cola launches its rocket. */
	UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category="Cola|Manufacturing|Failure") bool HandleManufacturingPressureFailure(bool bTemporarySealFailure, bool bOverfill);
	virtual bool HandleManufacturingPressureFailure_Implementation(bool bTemporarySealFailure, bool bOverfill);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing|Seal") bool bSupportsTemporarySeal=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing|Seal", meta=(ClampMin="0.1",Units="s")) float TemporarySealDuration=4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing", meta=(ClampMin="0")) float SafeResidualPressure=.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing", meta=(ClampMin="0")) float ManufacturingPressurePerContentLevel=.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing", meta=(ClampMin="0")) float ManufacturingMaxSupplyPressure=3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing", meta=(ClampMin="1.01")) float ManufacturingOverfillFailureLevel=1.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cola|Manufacturing|Cap", meta=(ClampMin="0")) float ManufacturingLeakageLevelPerSecond=.05f;
	bool IsManufacturingFailureActive() const;
	bool bManufacturingFailureDispatched=false;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Manual Filling",meta=(ClampMin="0.01")) float ManualFillVentRate=.15f;
	/** Current prototype choice: reuse BP_Cola's original projectile flight. False selects Chaos thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cola|Pressure|Rocket")
	bool bUseOriginalRocketFlight = true;
	/** Optional nozzle socket on the shell. Without a socket, use the cap mount or the top of the shell. */
	UPROPERTY(EditDefaultsOnly, Category="Cola|Pressure|Rocket")
	FName RocketNozzleSocket = TEXT("Nozzle");
	/** False while waiting for a replicated content definition. OutColor remains a usable fallback. */
	bool TryGetRocketSprayColor(FLinearColor& OutColor) const;
	bool LaunchOriginalBottleRocket(bool bHighAccuracy = false);
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1", Units="cm")) float RocketMinDistance = 2000.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1", Units="cm")) float RocketMaxDistance = 8000.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1")) float RocketMinSpeed = 1500.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="1")) float RocketMaxSpeed = 4500.f;
	/** Zero chooses a new server seed per event; a positive seed repeats the same configuration. */
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0")) int32 RocketRandomSeed = 0;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0")) float RocketAcceleration = 1600.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Rocket", meta=(ClampMin="0", ClampMax="89", Units="Degrees")) float RocketDirectionVariation = 25.f;
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Cola|Pressure|Rocket") FColaRocketLaunchConfiguration RocketLaunchConfiguration;
	void UpdateOriginalRocketDischarge(float DeltaSeconds);
	TWeakObjectPtr<UShopProductionStationComponent> CapHoldingStation;
	TWeakObjectPtr<UShopProductionStationComponent> ManualFillingStation;
	/** A temporary seal bleeds slowly; holding the machine lever provides the faster controlled vent. */
	UPROPERTY(EditAnywhere,Category="Cola|Pressure|Manual Filling",meta=(ClampMin="0")) float TemporarySealVentRate=.08f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float ImpactPressureGain = .8f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float ShakePressureGain = .4f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetLiquidLevelPerSecond = .15f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetAccelerationPerPressure = 1600.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure") float JetPressureVentRate = .4f;


protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void UpdateRocketPlume();
	void StopRocketPlume();
	float GetHeldMovementShakeMultiplier() const;
	bool bRocketNozzleRegistered = false;

public:
	// -------------------------------------------------------------
	// Components
	// -------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<URocketProjectileComponent> RocketComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CapMeshComponent;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> AssemblyLiquidMesh;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> AssemblyLiquidSkeletalMesh;
	void EnforceModularVisuals();
	void UpdateMotionPressure(float DeltaSeconds);
	FVector PreviousAgitationVelocity = FVector::ZeroVector;
	FVector FilteredAgitationAcceleration = FVector::ZeroVector;
	bool bHasAgitationHistory = false;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0", Units="cm/s")) float ImpactAgitationSpeedThreshold = 200.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0", Units="cm/s")) float RunningSpeedThreshold = 350.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0")) float RunningPressureRate = .12f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0", ToolTip="Minimum acceleration in centimeters per second squared")) float AccelerationAgitationThreshold = 900.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0")) float AccelerationPressureGain = .8f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0", Units="deg/s")) float AngularAgitationThreshold = 180.f;
	UPROPERTY(EditAnywhere, Category="Cola|Pressure|Handling", meta=(ClampMin="0")) float AngularPressureRate = .35f;
	float GetInstalledCapMass() const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Rendering")
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterialInstance;

	// -------------------------------------------------------------
	// Liquid & Portions
	// -------------------------------------------------------------
	/** Compatibility mirror of canonical EventData.ContentLevel; capacity comes from the product definition. */
	UPROPERTY(ReplicatedUsing = OnRep_ColaLevel, EditAnywhere, BlueprintReadWrite, Category = "Cola|Liquid")
	float ColaLevel = 1.0f;

	UFUNCTION()
	virtual void OnRep_ColaLevel();

	/** Non-plastic fill-plus-foam launch threshold. Plastic uses product pressure tolerance scaled by strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Liquid")
	float StabilityTolerance = 2.0f;

	/** Compatibility fallback for CalculateUnderfillPenalty when no content definition is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Economy")
	float BasePrice = 10.0f;

	// -------------------------------------------------------------
	// Agitation & Foam
	// -------------------------------------------------------------
	/** Current foam pressure from shaking / dropping (0.0 to 10.0) */
	UPROPERTY(ReplicatedUsing = OnRep_FoamPressure, VisibleAnywhere, BlueprintReadOnly, Category = "Cola|Foam")
	float FoamPressure = 0.0f;

	UFUNCTION()
	virtual void OnRep_FoamPressure();

	/** G-force jerk threshold to register bottle shaking/agitation while held */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float AgitationJerkThreshold = 1.5f;

	/** Multiplier applied to foam surge when bottle is overfilled (> 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float OverfillSurgeMultiplier = 3.0f;

	/** Rate of foam dissipation when stationary (FInterpTo speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float FoamDecayRate = 1.2f;

	/** Impulse threshold on OnComponentHit to trigger impact agitation surge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cola|Foam")
	float DropImpactThreshold = 2500.0f;

	// -------------------------------------------------------------
	// Gameplay Functions
	// -------------------------------------------------------------
	virtual bool OnPlayerGrab_Implementation(USceneComponent* AttachToParent, FName SocketName = NAME_None) override;

	/** Compatibility wrapper calling FillBottleAtRate(FillStrength, 1.0f). */
	UFUNCTION(BlueprintCallable, Category = "Cola|Liquid")
	virtual void FillBottle(float FillStrength);

	/** Computes agitation from velocity jerk / delta acceleration, updates foam & morph targets */
	UFUNCTION(BlueprintCallable, Category = "Cola|Foam")
	virtual void UpdateFoamPressure(float DeltaSeconds);

	/** Launches bottle into erratic zigzag rocket flight */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rocket")
	virtual void LaunchBottle();

	/** Attaches bottle cap mesh (PID determines style/material) */
	UFUNCTION(BlueprintCallable, Category = "Cola|Cap")
	virtual void AddCap(int32 InProductID);

	/** Detaches the physical cap through the manager; fallback mesh debris expires after five seconds. */
	UFUNCTION(BlueprintCallable, Category = "Cola|Cap")
	virtual void RemoveCap(float RemovingStrength = 1.0f, ECapRemovalOperation Operation = ECapRemovalOperation::EjectPhysics);

	/** Compatibility query returning the difference from the configured linear target-volume quote. */
	UFUNCTION(BlueprintPure, Category = "Cola|Economy")
	virtual float CalculateUnderfillPenalty(float TargetFill = 1.0f) const;

	/** Returns the configured portion-price quote with temperature and expiry rules. */
	UFUNCTION(BlueprintPure, Category = "Cola|Economy")
	virtual float CalculateSalePayout(float TargetFill = 1.0f) const;

	/** Updates modular liquid material slot 0, or slot 1 of the legacy combined bottle mesh. */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rendering")
	virtual void UpdateDynamicMaterial(bool bUpdateVelocity);

	/** Updates SkeletalMesh morph targets ("EmptyLevel" and "OverfullLevel") */
	UFUNCTION(BlueprintCallable, Category = "Cola|Rendering")
	virtual void UpdateMorphTargets();

	UFUNCTION()
	virtual void HandleOnBecomeActive(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform ActiveTransform);

	UFUNCTION()
	virtual void HandleOnBeforeReturnToRest(AActivePropActor* Actor, int32 InISID, int32 InProductID, FTransform RestTransform);

protected:
	/** Hit event handler for drop impacts */
	UFUNCTION()
	virtual void HandleBottleComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Rocket projectile delegate handlers */
	UFUNCTION()
	virtual void HandleRocketHitTarget(AActor* HitActor, const FHitResult& HitResult);

	UFUNCTION()
	virtual void HandleRocketFuelDepleted();

	UFUNCTION()
	virtual void HandleRocketSettled();

private:
	float GetAppliedSealDuration() const;
	float LiquidHeightCm = 150.f;
	float LiquidRadiusCm = 35.f;
	float LiquidResponseGain = 1.f;
	float LiquidPlaneOffset = 0;
	FVector LastLiquidLocalNormal = FVector::ZeroVector;
	float PortionVolumeMl = 20.f;
	float ContentDensityGPerMl = 1.04f;
	float LastLiquidFill = -1;
	bool bMorphInitialized = false;
	FGuid PressureReleaseEpisode;
	float PressurePublishElapsed = 0;
	float LastPublishedPressure = 0;
	float RocketInitialContentLevel = 0;
	float RocketInitialPressure = 0;
	FVector PrevLocation = FVector::ZeroVector;
	FVector PrevVelocity = FVector::ZeroVector;
	FRotator PrevRotation = FRotator::ZeroRotator;
	float PrevGForce = 0.0f;
	int32 WarmupFramesRemaining = 0;
	float HeldGracePeriod = 0.0f;
	bool bIsFirstActiveFrame = true;
	bool bWasHeldLastFrame = false;
};
