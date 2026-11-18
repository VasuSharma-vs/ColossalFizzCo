#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CableFeatureTypes.h"
#include "Components/CablePullTypes.h"
#include "Engine/TimerHandle.h"
#include "CollisionCableComponent.generated.h"

UENUM(BlueprintType)
enum class ECableCollisionMethod : uint8
{
    LineTraces UMETA(DisplayName="Line Traces (Fast)"),
    Sweeps UMETA(DisplayName="Collision Sweeps (Thickness)")
};

/**
 * A world-colliding rope with budgeted line traces or swept collision.
 * Point mass is handled by the rope solver; physics attachments receive load forces.
 * Collision requires query-enabled blocking geometry. Impossible/embedded anchor
 * configurations are reported rather than silently treated as collision free.
 */
UCLASS(ClassGroup=(Physics), meta=(BlueprintSpawnableComponent), DisplayName="Collision Cable")
class COLOSSALFIZZCO_API UCollisionCableComponent : public UMeshComponent
{
    GENERATED_BODY()
public:
    UCollisionCableComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void OnRegister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void SetComponentTickEnabled(bool bEnabled) override;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual void SendRenderDynamicData_Concurrent() override;
    virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual FMatrix GetRenderMatrix() const override;
    virtual int32 GetNumMaterials() const override { return 1; }
    virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
    virtual bool HasAnySockets() const override { return true; }
    virtual bool DoesSocketExist(FName SocketName) const override;
    virtual void QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const override;
    virtual FTransform GetSocketTransform(FName SocketName, ERelativeTransformSpace Space = RTS_World) const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable") bool bAttachStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable") bool bAttachEnd = true;
    UPROPERTY(EditAnywhere, Category="Cable") FComponentReference AttachEndTo;
    UPROPERTY(EditAnywhere, Replicated, Category="Cable") FName AttachEndToSocketName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Cable", meta=(MakeEditWidget=true)) FVector EndLocation = FVector(1600,0,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable", meta=(ClampMin="1")) float CableLength = 1600;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_CableFeatures, Category="Cable", meta=(ClampMin="1", ClampMax="256")) int32 NumSegments = 32;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Simulation", meta=(ClampMin="0.001", ClampMax="0.033333")) float SubstepTime = 1.f / 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Simulation", meta=(ClampMin="1", ClampMax="32")) int32 SolverIterations = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Simulation") bool bEnableStiffness = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Simulation", meta=(ClampMin="0", ClampMax="1")) float Stiffness = .1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Simulation", meta=(ClampMin="0")) float Damping = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Simulation", meta=(ClampMin="1", ClampMax="32")) int32 MaxSubsteps = 8;
    /** Never use a huge timestep to catch up after a hitch; excess simulation time is dropped. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") float DroppedSimulationTime = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features", meta=(DisplayName="Enable World Collision")) bool bEnableCollision = true;
    /** Independent of world collision. Non-neighboring spans repel each other. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features") bool bEnableSelfCollision = false;
    /** Sleep quiet points and disable the simulation tick once the entire cable settles. A separate wake monitor remains active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features") bool bEnableSleeping = true;
    /** Mass affects constraints, impulses, pulling acceleration and resulting grab drive. Fixed points are configured separately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features") bool bEnablePointWeight = true;
    /** One switch controls point grabbing, combined directional pulling and distance-based influence. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features", meta=(DisplayName="Enable Grabbing and Pulling")) bool bEnableGrabbing = true;
    /** Restrict actual movement of holders/drivers, including jumping and falling, when cable length or grab resistance requires it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features") bool bEnableMovementRestrictions = true;
    /** Intermediate render samples only; does not change simulation-point count or cross-section sides. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Features") bool bEnableVisualSubdivision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Self Collision", meta=(EditCondition="bEnableSelfCollision", ClampMin="0", Units="cm")) float SelfCollisionThickness = 0;
    /** Zero thickness uses Cable Width. Exclude spans whose indices differ by this many or fewer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Self Collision", meta=(EditCondition="bEnableSelfCollision", ClampMin="1", ClampMax="16")) int32 SelfCollisionNeighborExclusion = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Self Collision", meta=(EditCondition="bEnableSelfCollision", ClampMin="1", ClampMax="4")) int32 SelfCollisionIterations = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0", Units="cm/s")) float SleepVelocityThreshold = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0.05", Units="s")) float SleepDelay = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0.1", Units="cm/s")) float WakeThreshold = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0.01", Units="cm")) float WakeDisplacementThreshold = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0.01", Units="cm")) float SleepConstraintTolerance = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Sleeping", meta=(EditCondition="bEnableSleeping", ClampMin="0.05", ClampMax="1", Units="s")) float WakeMonitorInterval = .2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Weight", meta=(EditCondition="bEnablePointWeight", ClampMin="0.001", Units="kg")) float PointMassKg = 1;
    /** Index matches simulation points. Zero/missing entries use Point Mass Kg; zero mass never means fixed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Weight", meta=(EditCondition="bEnablePointWeight")) TArray<float> PointMassOverrides;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Weight") TArray<int32> FixedPointIndices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Weight", meta=(ClampMin="0.001", Units="kg")) float GrabReferenceMassKg = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Weight", meta=(ClampMin="0", Units="N")) float MaxAttachmentForceNewtons = 5000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Grabbing", meta=(EditCondition="bEnableGrabbing")) FCableGrabSettings DefaultGrabSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Grabbing", meta=(EditCondition="bEnableGrabbing", ClampMin="1", Units="cm")) float MaxGrabDistance = 350;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Movement Restrictions", meta=(EditCondition="bEnableMovementRestrictions", ClampMin="0", Units="cm")) float GrabMovementAllowance = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Movement Restrictions", meta=(EditCondition="bEnableMovementRestrictions", ClampMin="0", Units="cm")) float LengthSlackTolerance = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Movement Restrictions") bool bStartAttachmentIsWorldAnchor = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Movement Restrictions") bool bEndAttachmentIsWorldAnchor = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering", meta=(EditCondition="bEnableVisualSubdivision", ClampMin="1", ClampMax="8")) int32 VisualSubdivisions = 2;
    /** 0 keeps straight spans; 1 interpolates a smooth curve through the simulation points. Does not add physics points. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering", meta=(EditCondition="bEnableVisualSubdivision", ClampMin="0", ClampMax="1")) float VisualSmoothing = 1;
    /** Optional artistic limit. Off lets subdivision visibly round bends, independently of the collision skin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering", meta=(EditCondition="bEnableVisualSubdivision")) bool bLimitVisualDeviation = false;
    /** Maximum distance from a straight simulated span when Limit Visual Deviation is enabled. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering", meta=(EditCondition="bEnableVisualSubdivision && bLimitVisualDeviation", ClampMin="0", Units="cm")) float MaxVisualDeviation = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Networking") bool bEnableNetworkSync = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Networking", meta=(ClampMin="1", ClampMax="30", Units="Hz")) float SnapshotRate = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Networking", meta=(ClampMin="0.01", Units="cm")) float SnapshotPositionThreshold = .1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Networking", meta=(ClampMin="1", ClampMax="60")) float SnapshotInterpolationRate = 15;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") bool bCableSleeping = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 SleepingPointCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 SelfCollisionPairChecks = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 RenderGeometryRevision = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 SnapshotCount = 0;
    /** Total independent broad-phase wake checks, normally at 5 Hz only while some points sleep. Separate from per-tick surface queries. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 WakeQueryCount = 0;
    /** High quality mode: swept motion and all awake points/spans during substeps and solver passes, including the visual curve. Disable for the lower-CPU rotating point budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_CableFeatures, Category="Cable|Collision", meta=(DisplayName="Full Collision Queries (High CPU)", EditCondition="bEnableCollision")) bool bUseFullCollisionQueries = false;
    /** 1 checks spans every solver pass. Larger values reduce CPU; the last pass and every point's movement are always checked in full mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(EditCondition="bEnableCollision && bUseFullCollisionQueries", ClampMin="1", ClampMax="32")) int32 FullCollisionSolverInterval = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(EditCondition="bEnableCollision && !bUseFullCollisionQueries")) ECableCollisionMethod CollisionMethod = ECableCollisionMethod::LineTraces;
    /** Shared by both methods. Rotating point budget per component tick, never per substep/solver iteration. Clamped to 1..NumSegments+1. A selected point also checks one adjoining segment. Low budgets can miss fast contacts between checks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(EditCondition="bEnableCollision && !bUseFullCollisionQueries", ClampMin="1", ClampMax="257")) int32 CollisionPointsPerFrame = 8;
    /** Coulomb friction coefficient: holds resting surface contacts below the available load, and slows sliding per simulation substep. Zero permits free sliding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(ClampMin="0", ClampMax="1")) float CollisionFriction = .28f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(ClampMin="0.01")) float CollisionSkin = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bTraceComplex = false;
    /** Stop an attached endpoint at walls instead of teleporting it through them. May lag its target. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bSweepAttachedEndpoints = true;
    /** Ignore the mounting component, not its entire actor (which may also contain walls). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bIgnoreAttachedComponent = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bIgnoreOwner = true;
    /** Ignore the Pawn collision channel and player-controlled pawn actors, including players using custom object channels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bIgnorePlayerCollision = true;
    /** Ease existing penetrations out over time. Disabling restores immediate surface-normal correction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision|Recovery") bool bSmoothCollisionRecovery = true;
    /** Push away from the hit component's bounds center. Fall back to its surface normal if the direction is zero, inward or too tangential. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision|Recovery", meta=(EditCondition="bSmoothCollisionRecovery")) bool bUseBoundsCenterRecovery = true;
    /** Recovery speed grows with penetration distance, in inverse seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision|Recovery", meta=(ClampMin="0.1", EditCondition="bSmoothCollisionRecovery")) float CollisionRecoveryRate = 8.f;
    /** Minimum push-out speed in cm/s, avoiding an endless asymptotic approach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision|Recovery", meta=(ClampMin="0", EditCondition="bSmoothCollisionRecovery")) float MinCollisionRecoverySpeed = 5.f;
    /** Maximum collision correction speed in cm/s, shared by all contacts/substeps/iterations for each point. Regular forces can move the rope faster. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision|Recovery", meta=(ClampMin="1", EditCondition="bSmoothCollisionRecovery")) float MaxCollisionRecoverySpeed = 100.f;
    /** Prevent BP_Cable's five-second tick shutdown from disabling collision with moving objects. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision") bool bKeepCollisionAwake = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Collision", meta=(ClampMin="1", ClampMax="16")) int32 CollisionRecoveryIterations = 4;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") bool bHasUnresolvedCollision = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") bool bAttachmentBlocked = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 CollisionQueryCount = 0;
    /** Additional shape queries used to keep the smoothed render path outside blocking geometry in full mode. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") int32 RenderCollisionQueryCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") TArray<int32> LastCollisionPointIndices;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") float LastSimulationTimeMs = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, AdvancedDisplay, Category="Cable|Diagnostics") float MaxRecoveryDistanceThisFrame = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Forces") FVector CableForce = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Forces") float CableGravityScale = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering", meta=(ClampMin="0.1")) float CableWidth = 10;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cable|Rendering", meta=(ClampMin="3", ClampMax="16")) int32 NumSides = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cable|Rendering") float TileMaterial = 1;

    UFUNCTION(BlueprintCallable, Category="Cable") void SetAttachEndToComponent(USceneComponent* Component, FName SocketName = NAME_None);
    UFUNCTION(BlueprintCallable, Category="Cable") void SetAttachEndTo(AActor* Actor, FName ComponentProperty, FName SocketName = NAME_None);
    UFUNCTION(BlueprintCallable, Category="Cable") AActor* GetAttachedActor() const;
    UFUNCTION(BlueprintCallable, Category="Cable") USceneComponent* GetAttachedComponent() const;
    UFUNCTION(BlueprintCallable, Category="Cable") void GetCableParticleLocations(TArray<FVector>& Locations) const;
    /** Explicit reset/teleport. The supplied world-space path must have NumSegments+1 points. */
    UFUNCTION(BlueprintCallable, Category="Cable") bool SetCableParticleLocations(const TArray<FVector>& Locations);
    UFUNCTION(BlueprintCallable, Category="Cable") void ResetCable();
    /** True only if the complete cable capsules are clear of blocking query geometry. */
    UFUNCTION(BlueprintCallable, Category="Cable|Collision") bool IsCableCollisionFree() const;
    UFUNCTION(BlueprintCallable, Category="Cable|Features") void SetCableFeatureEnabled(ECableFeature Feature, bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="Cable|Sleeping") void WakeCable();
    UFUNCTION(BlueprintCallable, Category="Cable|Sleeping") void WakePoint(int32 PointIndex, bool bWakeNeighbors = true);
    UFUNCTION(BlueprintPure, Category="Cable|Sleeping") bool IsPointSleeping(int32 PointIndex) const;
    UFUNCTION(BlueprintCallable, Category="Cable|Sleeping") void CheckForWakeSources();
    UFUNCTION(BlueprintPure, Category="Cable|Weight") float GetPointMass(int32 PointIndex) const;
    UFUNCTION(BlueprintCallable, Category="Cable|Weight") bool SetPointMass(int32 PointIndex, float MassKg);
    UFUNCTION(BlueprintCallable, Category="Cable|Weight") bool SetPointFixed(int32 PointIndex, bool bFixed);
    /** Impulse in kg*cm/s, not a movement rate. Authoritative caller only. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cable|Weight") bool AddImpulseToPoint(int32 PointIndex, FVector Impulse);
    UFUNCTION(BlueprintPure, Category="Cable|Grabbing") int32 FindNearestCablePoint(FVector WorldPosition, float MaxDistance = 350) const;
    UFUNCTION(BlueprintPure, Category="Cable|Grabbing") bool FindCablePointAlongRay(FVector Start, FVector Direction, float Reach, float Radius, int32& PointIndex, FVector& HitPosition, float& RayDistance) const;
    /** Shared-server API. Remote players use their owned ShopPlayerInteractionComponent RPC boundary. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cable|Grabbing") bool BeginPointGrab(AActor* Participant, int32 PointIndex, FVector Target, const FCableGrabSettings& Settings, USceneComponent* TargetComponent = nullptr, FName SocketName = NAME_None);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cable|Grabbing") bool UpdatePointGrab(AActor* Participant, FVector Target);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Cable|Grabbing") bool EndPointGrab(AActor* Participant);
    UFUNCTION(BlueprintPure, Category="Cable|Grabbing") TArray<FCableGrabState> GetActiveGrabs() const;
    UFUNCTION(BlueprintPure, Category="Cable|Grabbing") void GetDirectGrabVelocities(TArray<FVector>& Velocities) const;
    UFUNCTION(BlueprintPure, Category="Cable|Movement Restrictions") AActor* ResolveMovementDriver(USceneComponent* Component) const;
    /** Called by the driver's existing movement callback; returns a sweep-safe requested root location. */
    FVector ConstrainDriverLocation(AActor* Driver, const FVector& OldLocation, const FVector& ProposedLocation) const;
    void GetMovementAttachments(TArray<FCableMovementAttachment>& Attachments) const;
    float GetCableMassKg() const;
    void NotifyConnectedPullApplied();
    UFUNCTION(BlueprintPure, Category="Cable|Rendering") void GetCableRenderLocations(TArray<FVector>& Locations) const;
    bool HasSimulationAuthority() const;

private:
    struct FParticle
    {
        FVector Position = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        FVector ContactNormal = FVector::ZeroVector;
        FVector ContactCorrectionThisStep = FVector::ZeroVector;
        FVector SelfContactCorrectionThisStep = FVector::ZeroVector;
        bool bSleeping = false;
        float QuietTime = 0;
        /** Adjacent span lengths captured only on entry to sleep; distinguish an existing residual from a new pull. */
        double SleepPreviousSpanLength = -1;
        double SleepNextSpanLength = -1;
    };
    TArray<FParticle> Particles;
    struct FLineContact
    {
        FVector Point = FVector::ZeroVector;
        FVector Normal = FVector::ZeroVector;
        TWeakObjectPtr<UPrimitiveComponent> Component;
        double Alpha = .5;
    };
    TArray<FLineContact> PointLineContacts;
    TArray<FLineContact> SegmentLineContacts;
    TArray<FVector> LastPointTracePositions;
    TArray<FVector> LastSegmentTracePositions;
    int32 NextLineTracePoint = 0;
    TArray<double> RecoveryDistanceUsed;
    TArray<double> RecoveryDistanceLimit;
    float RecoveryDeltaTime = 0;
    TArray<FVector> LastClearPositions;
    FCollisionQueryParams QueryParams;
    FCollisionResponseParams ResponseParams;
    bool bResettingCable = false;
    bool bInstallingWakeSensor = false;
    bool bSelfContactUnresolved = false;
    struct FSelfContact
    {
        int32 A=0,B=0;
        double TA=0,TB=0;
        double NormalCorrectionLambda=0;
        FVector Normal=FVector::UpVector;
    };
    TArray<FSelfContact> SelfContacts;
    uint32 LastFeatureSignature = 0;
    float SnapshotAccumulator = 0;
    FVector LastWakeStart = FVector::ZeroVector;
    FVector LastWakeEnd = FVector::ZeroVector;
    UPROPERTY(Transient) TArray<FCableGrabState> ActiveGrabs;
    TArray<FVector> PublishedPoints;
    TArray<FVector> RenderInputPoints;
    uint32 PublishedRenderSignature = 0;
    FVector PublishedRenderOrigin = FVector(UE_BIG_NUMBER);
    float PublishedWidth = -1;
    float PublishedTiling = -1;
    int32 PublishedSubdivisions = -1;
    TArray<FVector> ClientSnapshotTargets;
    FTimerHandle WakeMonitorTimer;
    UPROPERTY(Transient) TObjectPtr<class UBoxComponent> WakeSensor;
    TMap<TWeakObjectPtr<USceneComponent>, FTransform> ObservedTransforms;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> WakeNearbyBodies;
    TArray<TWeakObjectPtr<class UCableMovementConstraintComponent>> MovementBindings;
    uint64 LastConnectedPullFrame=MAX_uint64;
    UPROPERTY(ReplicatedUsing=OnRep_SimulationSnapshot) FCableSimulationSnapshot SimulationSnapshot;
    UPROPERTY(Replicated) TObjectPtr<USceneComponent> ReplicatedEndComponent;
    bool IsPinned(int32 Index) const;
    double InversePointMass(int32 Index) const;
    bool IsWorldAnchor(int32 Index) const;
    bool HasCollision() const;
    float CollisionRadius() const;
    void GetEndPositions(FVector& Start, FVector& End) const;
    void RefreshCollisionParams();
    void PublishSimulation();
    void BuildCableRenderLocations(TArray<FVector>& Locations) const;
    void ConstrainRenderPath(TArray<FVector>& Locations);
    void Simulate(float Dt, const FVector& StartTarget, const FVector& EndTarget);
    void CheckCollisionPointBatch(const FVector& StartTarget, const FVector& EndTarget);
    void ApplyCollisionVelocityResponse();
    struct FSurfaceFrictionBudget
    {
        int32 PointIndex=INDEX_NONE;
        FVector Normal=FVector::ZeroVector;
        FVector Applied=FVector::ZeroVector;
    };
    void ApplyContactFriction(float Dt,const TArray<FVector>& Before,TArray<FSurfaceFrictionBudget>& Budgets);
    void ResetCollisionHistory();
    void ProjectLineContacts();
    void ProjectLinePoint(int32 Index, const FLineContact& Contact);
    void ProjectLineSegment(int32 Index, const FLineContact& Contact);
    bool QueryPointMotion(const FVector& From, const FVector& To, FHitResult& Hit);
    bool RefreshLineContact(const FVector& Position, FLineContact& Contact);
    bool MakePenetrationContact(const FVector& From, const FVector& Position, const FHitResult& Hit, FLineContact& Contact);
    void MoveContactParticle(int32 Index, const FVector& Correction, const FLineContact& Contact);
    FVector ConstrainMotionToKnownContacts(int32 Index, FVector Motion) const;
    FVector ConstrainFullCollisionMotion(int32 Index, FVector Motion);
    void UpdateKnownCollisionStatus();
    void SolveDistance(int32 A, int32 B, float RestLength, float Strength);
    bool RecoverContacts();
    bool PathIsClear(const TArray<FVector>& Positions) const;
    void SolveSelfCollision(const TArray<FVector>& Before);
    FVector ConstrainSelfContactDirection(int32 Index,FVector Direction) const;
    void ApplySelfContactFriction(const TArray<FVector>& Before);
    void ApplySelfCollisionVelocityResponse();
    void ApplyGrabDrive(float Dt);
    void ApplyAttachmentForces(float Dt);
    void UpdateSleeping(float Dt, const TArray<FVector>& Before);
    void RefreshWakeSources();
    void CheckSleepingBodyProximity();
    void OnObservedTransform(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);
    void RefreshMovementBindings();
    uint32 FeatureSignature() const;
    void PublishNetworkSnapshot(float Dt, bool bForce = false);
    UFUNCTION() void OnRep_CableFeatures();
    UFUNCTION() void OnRep_SimulationSnapshot();
    UFUNCTION() void OnWakeSensorOverlap(UPrimitiveComponent* Overlapped, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Hit);
};
