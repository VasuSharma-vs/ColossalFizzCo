# BP_Cola technical architecture

Updated: 2026-11-18. This document describes the current Blueprint and C++ implementation. Product catalogs and data assets supply runtime configuration; native defaults remain editable settings. The pressurized-supply update builds as Editor 18103 and passes 70/70 selected native regressions (19 with warnings). Rendered and real remote-client acceptance remains pending; earlier subsystem verification is recorded separately below.

The [pressurized container and hose path](Pressurized_Cola_Container_2026-11-18.md) delivers actual accepted units into the seated bottle through the Prop Manager. `OnManufacturingSupplyReceived` receives the incoming pressure and delivered rate; `ManufacturingMaxSupplyPressure` defaults to 3. Exceeding that limit after delivery triggers the bottle's existing failure immediately, even with the filling lever held. Normal plastic cola uses its existing rocket path. The machine stops flow and releases the bottle; it does not choose or create the bottle's failure behavior.

The [manual filler client visual repair](Manual_Filler_Client_Visuals_2026-11-18.md) validates prop class and logical instance identity during client pool reuse, keeps local slots distinct, and protects placed machines from automatic pool shrinking. Its separate host/client/late-join checks pass on 18103. This does not replace the pending rendered and remote acceptance for the new hot-supply gameplay itself.

## 1. Ownership and inheritance

The master Blueprint is `/Game/Props/Colas/BP_Cola`. Its native parent is `AColaPropActor`, declared in [ColaPropActor.h](../Source/ColossalFizzCo/Public/Props/ColaPropActor.h).

```text
AActor
  AActivePropActor : IWorldAffordanceInterface
    AColaPropActor
      BP_Cola
        BP_PlastickBottel
        BP_SpicyCola
        BP_SugerCrushCola
```

These three child Blueprints were verified in the editor. Their names do not define flavor, pressure thresholds, prices or hazards by themselves; inspect the resolved product and content definitions.

| Owner | Responsibility |
|---|---|
| `BP_Cola` | Authored component settings and `BPI_World_Intractions_System` interaction wrappers. No Blueprint-declared gameplay variables remain after cleanup. |
| `AActivePropActor` | Identity, interaction/carrying, active lifecycle, consumption, local state mirrors, custom primitive data and base replication. |
| `AColaPropActor` | Bottle mass, supported filling content, cap closure, temporary seals, pressure/failure policy, impact damage, liquid motion, materials, morph targets and rocket integration. |
| `APropManager` | Persistent instance records, shared definitions, assemblies, liquid transfers/releases, representation changes, saves and synchronization. |
| Manager-owned `UASMComponent` | Pool of active prop actors used when promoting resting instances. |
| Manager-owned `UPropBatchUpdateComponent` | Batched temperature, thermal damage, expiry and representation updates. |

ASM means the active pooled actor representation. A resting ISM is an instanced static mesh representation. `UASMComponent` belongs to the manager, not each bottle.

## 2. Components and configuration

The Blueprint has four default components:

| Component | Type | Purpose |
|---|---|---|
| `PropMesh` | `UStaticMeshComponent` | Actor root and collision/physics body. Receives hit callbacks and is moved by rocket flight. Also provides a static visual fallback. |
| `SkeletalMeshComponent` | `USkeletalMeshComponent` | Visual child of `PropMesh`, with collision disabled by the native constructor. Displays deformation through morph targets. |
| `PhysicsSimComponent` | `UStaticMeshPhysicsSimulationComponent` | Targets `PropMesh`; coordinates `AtRest`, `InMotion`, `Held`, `Unsettled` and `Settling`. Native defaults: 3-second unsettled delay, 10 cm/s linear tolerance and 15 deg/s angular tolerance. |
| `RocketProjectile` | `URocketProjectileComponent` | Authored Blueprint component assigned to native `RocketComp`. Handles flight, collisions, fuel depletion, falling and settling. |

`AActivePropActor` creates the first three components. `PostInitializeComponents()` finds the authored rocket or creates a replicated `NativeBottleRocket` fallback, then binds native lifecycle, impact and rocket delegates.

The verified master references `/Game/Assest/DataAssets/DA_Cola`. That asset supplies `SM_PlasticBottle`, `SKM_PlasticBottle`, plastic material type, nominal capacity of 135 portions, empty mass of 0.05 kg and `MaxOverfillLevel` of 1.7. Construction/product-definition application assigns meshes; a null static mesh on the unconstructed class default object is not the runtime configuration.

Additional runtime objects:

- `AssemblyLiquidMesh` / `AssemblyLiquidSkeletalMesh`: presentation for an attached liquid product. The manager creates assembly components and may add a skeletal visual for deformation.
- Physical cap: an attached product with its own ISID, reciprocal bottle/cap IDs and assembly edge. It can have its own active actor when detached. `CapMeshComponent` is the compatibility mesh fallback.
- `DynamicMaterialInstance`: transient material created/rebound for the selected liquid mesh and slot.
- `FColaLiquidMotion`: cosmetic C++ struct holding motion history, tilt, waves and fluid spin; not an Actor Component or saved product state.
- `DebugText`: optional base-class debug presentation, null by default; not a fifth default Blueprint component.

## 3. Data ownership

`ISID` identifies an individual instance. `ProductID` selects its product definition; `ContentID` selects its liquid definition. `ASMID` identifies an active actor pool slot. Assembly children have independent instance identities.

| Data | Contents |
|---|---|
| `FPropProductDefinition` | Shared meshes, physics, mass, portion capacity, bottle material, cap compatibility, liquid visual product and failure transformations. |
| `FPropContentDefinition` | Shared liquid material, density, pressure multiplier, specific heat, price, serving temperature, taste, stimuli and expiry. |
| `FPropEventUpdateData` | Discrete persistent fill, content ID, strength, cap seal/type/links, cleanliness, creation times, manufacturing seal/supply fields and `EventSequence`. |
| `FPropContinuousUpdateData` | One `Temperature`, `SealedPressure` and thermal integration timestamp `LastUpdatedTime`. |
| `FPropInstanceUniqueData` | Event/continuous state container for products that enable unique instance data. |

The manager maintains `UniqueDataByISID`; an active actor holds a `LocalUniqueData` mirror. `EmitPropEventUpdate()` submits changed fields through dirty masks. External event updates reject older sequences; continuous updates reject timestamps older than accepted discrete/continuous state.

`EventData.ContentLevel` is canonical normalized fill: 0 empty, 1 nominal full, above 1 overfill within configured capacity. Fractional portions derive from fill and nominal capacity. Native `ColaLevel` is a replicated compatibility/presentation mirror; setting it directly does not perform a fill operation.

Native `FoamPressure`, `InteractionPressure`, `StabilityTolerance` and compatibility methods remain. The cleanup removed Blueprint duplicates, not native API signatures.

## 4. Retained Blueprint graphs

The ten retained graphs preserve their previous executable flow:

| Graph | Behavior |
|---|---|
| `UserConstructionScript` | Empty custom body; native construction still applies configuration. |
| `EventGraph` | `EventMoveActorToLocation(Location)` calls `MoveHeldToLocation(Location)`. |
| `Request Attach to Component` | Calls `AttachForInteraction(Component, Socket Name)` and returns its result. |
| `Request Detach from Component` | Calls `DetachForInteraction()` and returns its result plus `self`. |
| `Can grab` | Returns `PhysicsSimComponent.IsGrabbable()`. |
| `Can Intract` | Returns whether physics state differs from `Held`, plus the instance ISID. |
| `Get Product Name` | Queries `PropManagerReference.GetPropData()` for the actor's ISID; returns the name or `None` on failure. |
| `Get Rope Tenction` | Interface implementation returning zero vector and scalar. |
| `is being traced` | Interface implementation returning `true`. |
| `Get Empty Pocket` | Interface implementation returning `false` and `0`. |

Constant-return interface functions remain because interface callers can invoke them. Their simple bodies do not make them removable. The empty construction graph remains as the normal construction entry point.

## 5. Initialization, tick and pooling

`PostInitializeComponents()` binds activation/rest delegates, `PropMesh.OnComponentHit`, and rocket movement/hit/fuel-depleted/settled callbacks to native handlers.

Promotion restores contents/cap presentation, resets motion history and rebinds materials. Warmup frames, pickup grace, rest interpolation and teleport guards prevent pool relocation or hand interpolation from producing false agitation.

Active tick runs the base implementation, skips inactive pooled actors, updates plastic or manufacturing pressure on the server, advances cosmetic liquid motion, samples velocities, calculates server agitation, and updates materials/morphs. Contact impulses are calibrated against reference mass before pressure/damage evaluation. Gameplay mass includes empty bottle, actual liquid and installed cap.

```text
Resting ISM -> manager promotion / pooled actor binding
            -> active interaction / physics / liquid presentation
            -> physics and cosmetic settling -> CanDemoteToISM()
            -> persist state / reset transients / restore ISM
```

The base demotion gate rejects held, moving, filler-seated, interpolating, volume-kept-active or still-flying actors. The cola override requires settled liquid motion, negligible pressure expansion and agitation, and no accepted failure. Filled uncapped bottles also check `bAllowUncappedFilledISMDemotion` and attached-cap state.

`ResetTransientPoolState()` clears liquid motion, material/assembly pointers, foam/interaction mirrors, launch configuration, impact history, release episode and fallback cap, and resets the rocket. Initial client binding can preserve an already-received flight snapshot through `ResetForInstanceBinding()`.

## 6. Filling and liquid transfer

```cpp
bool SetBottleContentType(int32 NewContentID);
float FillBottleAtRate(float Rate, float DeltaSeconds);
void FillBottle(float FillStrength); // Calls FillBottleAtRate(FillStrength, 1.0f)
void RemoveBottleContent();
```

`FillBottleAtRate()` runs on authority and rejects invalid/non-positive inputs, positive cap seal, missing content identity/definition and accepted failure state. Rate is normalized bottle fill per second:

```text
Capacity = MaxOverfillLevel * StrengthRatio   // Plastic or HeavyMetal
Capacity = MaxOverfillLevel                   // Other materials
Flow = max(0, FillSlowdownCurve(CurrentFill))  // 1 without a curve
AcceptedLevel = min(Rate * DeltaSeconds * Flow, max(0, Capacity - CurrentFill))
ReturnedPortions = AcceptedLevel * NominalPortions
```

An accepted first fill restarts `ContentCreationTime`, preserves container creation time and emits the content-level update. `SetBottleContentType()` requires empty/clean state, strength strictly greater than 0.70 and no greater than 1, and assembly compatibility. `RemoveBottleContent()` clears fill and content identity.

Shop/source filling uses manager `BeginLiquidTransfer`, `TransferLiquid` and `EndLiquidTransfer` in [PropLiquidTransfer.cpp](../Source/ColossalFizzCo/Private/Props/PropLiquidTransfer.cpp). These validate supply, target, cap/temporary seal, cleanliness, strength, compatibility and stock/session ownership, and conserve accepted portions through atomic source/target updates. Same-content partial filling can resume. Filling changes the liquid child/amount while preserving physical bottle ProductID.

### Manual manufacturing and temporary seals

`BP_Cola` inherits the manufacturing API from `AColaPropActor`. `CanAcceptManufacturingContent()` checks the bottle's resolved product/content compatibility. `OnManufacturingLiquidReceived()` accounts for pressure and overfill. Cap attachment, tightening, leakage and failure evaluation also belong to the bottle. Persistent identities and conserved transfers still belong to `APropManager`.

The manual machine supplies player input, source/tray selection and visuals. Its cradle holds the bottle neck upward at rest and tilts the neck downward before liquid flow is permitted. The lever, cradle and capping-head visuals follow station/bottle state; they do not select the bottle's failure reaction.

In **Cola > Manufacturing**, configure seal support, seal duration (default 4 seconds), safe residual pressure (0.1), pressure per filled level (0.6), overfill failure level (1.1) and cap leakage. `AddManufacturingSeal()` installs protection on the bottle; the station consumes a physical seal atomically. A seal installed before filling or while the lever is held remains armed at full integrity. First lever release starts its deadline. Regrabbing the lever cannot renew it. `bTemporarySealArmed`, `bTemporarySealUsed`, `TemporarySealUntil` and `bManualFillPressure` persist in per-instance event state. An active seal prevents ISM demotion.

At expiry, unsafe pressure without a secured cap or station restraint invokes **Handle Manufacturing Pressure Failure**, a Blueprint-overridable event. Normal plastic cola uses rocket release; other materials use the containment-failure path. A secured cap ends temporary protection and remains subject to total-pressure tolerance. See [manufacturing behavior and controls](Cola_Manufacturing_Ownership_2026-11-17.md) for the complete sequence and editable settings.

## 7. Pressure, damage and flight

### Plastic pressure

`UsesPlasticPressure()` selects the path by resolved bottle material. While liquid remains:

```text
TotalPressure = max(0, SealedPressure)
              + ContentLevel * max(0, Content.PressureStrengthMultiplier)
Tolerance = max(epsilon, Product.PressureFailureThreshold * StrengthRatio)
```

`SealedPressure` stores agitation. Acceleration, rotation, running while held and calibrated impacts add pressure through `AddAgitationPressure()`, scaled by product pressure resistance. The manager clamps agitation to 100. `InteractionPressure` mirrors it; `FoamPressure` mirrors `clamp(InteractionPressure * 5, 0, 10)`.

`UpdatePlasticPressure()` handles decay, temporary manufacturing seals and rocket discharge; it also integrates manufacturing pressure for other bottle materials. Filling suppresses decay. Native settings are 0.5 per second while sealed/held closed, 4 per second when open, 0.15 per second for the manual-fill vent and 0.08 per second through a released temporary seal. Holding the lever with flow stopped uses the faster manual vent. Changed pressure normally publishes every 0.2 seconds, with a one-second nonzero heartbeat and a reliable final record at zero.

When sealed pressure exceeds tolerance and a station is not holding the cap closed, `StartPlasticPressureRelease()` releases a held bottle, detaches/ejects its cap and starts flight. Temporary-seal expiry has separate admission checks. This launch path does not inherently turn the bottle into a broken product.

### Other materials and impact damage

The compatibility foam path uses held/attached velocity jerk and drop impacts. Foam contributes to apparent volume, decays when agitation stops, and calls `LaunchBottle()` when `ContentLevel + FoamRatio` reaches `StabilityTolerance`. Plastic uses pressure tolerance instead.

`ApplyBottleImpact()` runs on authority, filters repeated nonzero impact IDs and applies a 0.05-second contact cooldown. Above-threshold impacts reduce persistent strength. Glass/concrete at zero strength submit breakage; diamond bypasses impact damage. Thermal damage runs through the batch updater. Pressure decay does not repair strength.

`SubmitFailureRequest()` accepts enum priority: breakage, overpressure burst, thermal failure, pressure release, cap failure, then other. Once accepted, equal/lower priority requests are rejected. It uses `AcceptedFailureType`, not a separate replicated failure-state machine or failure-GUID deduplication set.

Configured brittle fracture creates fragment assemblies/recipes, accounts for released liquid, separates an attached cap and releases the source instance. Other transformations choose `BrokenProductID` for breakage, `MeltedProductID` for thermal failure or `BurstBrokenProductID` for other requests when configured. Content/pressure removal follows request flags. Variant names alone do not spawn sticky/fire hazards in this class.

### Rocket integration

`bUseOriginalRocketFlight` defaults to `true`; plastic release can alternatively use Chaos pressure-thrust flight. Original flight moves `PropMesh`, configures random 3D zigzags and uses a server seed. Native default ranges are 2,000-8,000 cm travel and 1,500-4,500 cm/s speed, with 1,600 acceleration and a 25-degree direction cone. Distance/speed are sampled from those ranges, not multiplied by fill.

The component supports idle, tipping, dragging, rocketing, low-fuel sputter, spent falling and rest; configuration determines which stages run. Bottle launch now preserves authored flight and falling-bounce limits instead of forcing eight ricochets and four falling bounces.

`FallingSettings.PhysicsHandoffMode` selects `UseLegacySettings`, `Disabled`, `OnFuelDepleted` or `AfterBounceApex`. Legacy mode honors the three older physics/depletion switches; all false keeps scripted falling. An explicit enum choice overrides those switches. BP_Cola's preset uses `AfterBounceApex` with `HandoffAfterBounces = 1`: scripted fall, one scripted floor bounce, then physics at the rebound apex. Fuel depletion no longer forces an immediate Chaos handoff from the Cola callback; `EndScriptedThrust()` owns the policy.

`PoweredMotionSettings` provides an optional inline speed curve and separate acceleration/deceleration limits (2,200/1,800 cm/s²). Its X ranges are tipping 0–0.1, dragging 0.1–0.25, flight 0.25–0.8 and low-fuel climb 0.8–1; Y multiplies launch base speed. Default keys are `(0,.04)`, `(.1,.2)`, `(.25,.45)`, `(.45,1)`, `(.8,.4)`, `(1,.03)`. The native feature defaults off; BP_Cola's preset enables it.

Both planned and ballistic scripted descent use `FallAcceleration`, `SpentGravityScale`, `FallingSpeedMultiplier` and `TerminalVelocity`. After handoff, temporary linear/angular damping defaults to 2.5/5. Supported rest requires speed below 80 cm/s and rotation below 60 deg/s, plus a surface beneath the body's bounds for the settling duration. Mid-air low speed alone cannot settle the bottle; original damping is restored after flight/reset.

Plastic original-flight discharge reduces liquid/agitation according to traveled-distance progress on movement substeps. `GetPoweredTravelDistance()` includes the planned low-fuel climb, preventing the main-flight distance from draining the bottle prematurely. Released liquid accumulates under one `PressureReleaseEpisode` GUID through `ReleaseBottleLiquidPortions()`. Fuel depletion releases remaining liquid, clears agitation and refreshes mass/materials. Its unsettled-timer request respects the scripted motion lock. The settled callback clears the motion lock for normal pooling.

The physics helper protects both powered and spent scripted movement from automatic physics activation. At the chosen handoff it preserves actor movement replication through later settling transitions. Natural depletion keeps the predicted descent/rebound plan; an early external cutoff falls ballistically from the current pose. Planned rebound handoff uses the actual vertical maximum even when the two landing points have different heights.

Designer controls, compatibility details and build/test evidence are documented in [Rocket lifecycle control](Rocket_Lifecycle_Control_2026-11-17.md). BP_Cola's saved preset and inline curve keys are verified; restart UE5 to load the new native controls.

### Rocket contacts and receiver-owned responses

`RocketProjectile.bUseOverlapHitEvents` defaults to true. Scripted flight disables physics and uses `QueryOnly`; Chaos thrust and the falling physics handoff use `QueryAndPhysics`. Both overlap `Pawn`, `PhysicsBody`, `WorldDynamic` and the project's `ISM`/`ActiveISM` object channels, and block `WorldStatic`. Dynamic contacts report data without passive collision impulses/depenetration against the player. Static surfaces retain normal ricochet/physical collision. Pool reset restores the original collision responses, enabled mode and overlap-event setting. Disabling this option restores the legacy blocking path.

The server performs a multi-hit sphere sweep as well as listening for begin-overlap callbacks. Sweeps detect fast crossings and ISM contacts even when the target's `Generate Overlap Events` is disabled. Flight-plan and ground probes use the same collision-response filter, so overlapping players/props do not become planned ricochet surfaces. The component suppresses repeated contacts with the same actor/stable prop ID for `WorldHitRepeatDelay` (default 0.25 seconds). Clients simulate/present flight but cannot dispatch authoritative hit responses. The ejected cap and the source's own assembly are excluded.

`FWorldInteractionHit` contains an event GUID, source/target actor and primitive, full `FHitResult`, incoming and relative velocities (cm/s), source mass (kg), incoming normal momentum (`Impulse`, kg cm/s), `bApplyHitForce`, and optional `ICID`, `ISMI`, `ISID`. This impulse is descriptive data; receivers decide what to do with it. `bApplyHitForceOnTargets` on the rocket is permission, not an automatic impulse.

`UWorldInteractionHitLibrary::DispatchWorldHit()` sends `Receive World Hit` to native-interface actors, the contacted component and other native-interface receiver components on that actor. It falls back to the existing `BPI_World_Intractions_System` **Hit Actor** message for legacy receivers, preserving its four existing inputs (component, position, normal and momentum magnitude). Native-interface receivers take precedence on an object, preventing the same object from receiving both messages. The legacy signature cannot carry the GUID, source actor or force permission; use the native interface for those fields.

`BP_FirstPersonCharacter` does not implement the legacy world interaction interface. Its authored `RocketHitResponse` (`World Hit Response Component`) receives hits with automatic force disabled. Select that component and bind **On World Hit Received** to author player knockback, damage and other reactions using the full packet. The component and Blueprint were compiled and saved on 17 November; evidence is in `Saved/RocketWorldHits_20261116/PlayerReceiver.json`. For other actors, add the same component or implement **World Interaction Hit Interface / Receive World Hit** directly. The rocket never calls `LaunchCharacter`. Optional simulated-body force uses `Apply Bounded World Hit Impulse` and a receiver-selected speed-change cap. Choose one physical responder per actor to avoid applying force twice.

For managed instances, **ICID is the ISM component ID**, while **ISMI is the overlap body index / sweep `Hit.Item`**. The component/manager resolves this pair to the permanent **ISID**, then resolves the assembly root. Missing/invalid indices are rejected; no nearby instance is guessed. Resting props promote only when incoming momentum meets `PropManager.WorldHitPromotionImpulseThreshold` (default 20 kg cm/s). Promotion runs without an initial kick, then the same event is retargeted to the resulting ASM. Held props and self-assembly hits are rejected. Event GUIDs prevent duplicate promotion/damage processing.

`AActivePropActor::ReceiveWorldHit()` applies product damage from the original momentum and owns the optional physical response. `bApplyWorldHitForce` defaults true, `WorldHitImpulseScale` defaults 1, and `MaxWorldHitSpeedChange` defaults 250 cm/s. Force is capped using actual target mass. Turning off either the sender permission or receiver force still permits threshold-based promotion and damage. `SetInstanceToUnsettled()` also exposes an appended **Apply Hit Force** bool (default true, including its server request); false skips only the impulse, preserving impact damage. Existing callers retain their previous behavior.

Implementation: [RocketWorldHits.cpp](../Source/ColossalFizzCo/Private/Components/RocketWorldHits.cpp), [WorldInteractionHitInterface.h](../Source/ColossalFizzCo/Public/Interfaces/WorldInteractionHitInterface.h), [WorldInteractionHitLibrary.cpp](../Source/ColossalFizzCo/Private/Props/WorldInteractionHitLibrary.cpp), [PropWorldHits.cpp](../Source/ColossalFizzCo/Private/Props/PropWorldHits.cpp), [WorldHitResponseComponent.h](../Source/ColossalFizzCo/Public/Components/WorldHitResponseComponent.h).

Earlier contact-system validation recorded a successful native editor build and ten selected automation checks, including swept player/ISM contacts, static-surface filtering, unrelated trigger filtering, client rejection, duplicate suppression, bounded force, force-disabled promotion/damage, original flight, pressure flight, pawn contact and debug drawing. Evidence: `Saved/RocketWorldHits_20261116/NativeReport/index.json`. These earlier checks do not validate the latest lifecycle changes. Reopen the editor after native changes to load the updated classes.

### Rocket debug display

`RocketProjectile` exposes independent `Enable Debug Logs`, `Show On Screen Debug` and `Draw Debug Trajectory` options. Bottle initialization preserves their Blueprint/instance values, including a trajectory-only configuration. These are development diagnostics and the drawing code is excluded from Shipping builds.

Trajectory drawing runs while the component is flying: it shows the scripted flight plan/waypoints and a five-second movement trail. Pressure-physics flight and the falling physics handoff also draw the movement trail, direction and telemetry using the physics body's velocity. Chaos flight has no precomputed spline. Idle/resting bottles do not generate a path, and enabling the option does not create an editor preview; launch the bottle during Play or Simulate to see it.

## 8. Cap assembly

[PropCapAssembly.cpp](../Source/ColossalFizzCo/Private/Props/PropCapAssembly.cpp) implements:

- `AttachPhysicalCap()`: validates compatibility/proximity/ownership/state, installs an assembly edge and updates reciprocal IDs atomically.
- `ClosePhysicalCap(BottleID, Torque)`: seals in the product's torque sweet spot; otherwise writes a partial seal below 1. Excess torque can damage the bottle.
- `DetachPhysicalCap()`: clears reciprocal IDs/seal/type, removes the assembly edge and enables the cap's unsettled representation.
- `SynchronizeCapAssembly()`: refreshes runtime assembly presentation.

`AddCap()` synchronizes an existing physical cap or attaches a compatibility mesh; it is not a four-stage closing solver. `RemoveCap(float, ECapRemovalOperation)` delegates physical-cap detachment or removes/ejects the fallback. Its operation enum has `EjectPhysics` and `DropQuietly`; ejected fallback debris expires after five seconds. Cap product types such as twist are separate from removal operations.

## 9. Rendering and temperature

Active root/visual meshes receive four custom primitive data floats:

| Slot | Value |
|---|---|
| 0 | Canonical content level |
| 1 | Normalized expiry age |
| 2 | Temperature in Celsius |
| 3 | Normalized structural strength |

`UpdateDynamicMaterial()` targets modular liquid slot 0 or legacy combined mesh slot 1. If an expected liquid child is absent it waits for assembly creation. Creation depends on the selected mesh, slot and material; this function has no blanket positive-content/fill creation guard.

Material inputs include `Native Liquid Motion`, `Use Instance Liquid Data`, `Liquid Cached Fill`, `Liquid Surface Normal WS`, `Liquid Motion State`, `Liquid Plane Offset`, object velocities, `Use Dynamic Values` and `Liquid Level`. Visual fill clamps to 0-1; canonical overfill remains available for capacity/deformation. A cylindrical volume solver preserves fill under a tilted plane.

`UpdateMorphTargets()` drives `OverfullLevel` on the bottle and optional skeletal liquid. It drives `EmptyLevel` on a combined bottle without a separate liquid assembly. Plastic expansion is smoothed and depends on retained pressure; non-plastic apparent volume includes foam. `EnforceModularVisuals()` avoids duplicate static/skeletal presentation.

There is one persistent temperature: `ContinuousData.Temperature`. When enabled, the manager batch updater advances it toward ambient using exponential heat transfer, conductivity, thermal mass, heating/cooling multipliers and liquid specific heat. It evaluates thermal damage/failures for resting ISMs and active actors. Base `HasActiveThermalSources()` currently returns false and cola does not override it. There is no separate shell/content thermal solver or temperature-equilibration demotion gate.

## 10. Pricing and replication

`GetSaleQuote()` uses the manager scanner query when bound, otherwise the content definition. [ColaPricingLibrary.cpp](../Source/ColossalFizzCo/Private/Props/ColaPricingLibrary.cpp) computes:

```text
FullBaseCents = NominalPortions * PricePerPortion * 100
FillMultiplier = clamp(ContentLevel, 0, 1)
TemperatureDiscount = 0.20 * clamp(abs(Temperature - Optimum) / PenaltyRange, 0, 1)
SaleCents = round(FullBaseCents * FillMultiplier * (1 - TemperatureDiscount))
```

Underfill discounts are linear; overfill does not raise sale price. Temperature discount caps at 20%. Expiry marks the quote and sets compensation to original full base price rather than silently changing the original invoice. Aggregate bill rounding is separate.

`CalculateSalePayout(TargetFill)` returns the quote in currency units. `CalculateUnderfillPenalty(TargetFill)` returns the difference from full content price and can include temperature effects. Both retain but ignore the compatibility parameter. `BasePrice` is a fallback, not the portion-price authority.

Gameplay mutations are authoritative. Base replication includes identity, `LocalUniqueData`, interaction/state flags and velocities. Cola-specific replicated properties are `AcceptedFailureType`, `InteractionPressure`, `ColaLevel`, `FoamPressure` and `RocketLaunchConfiguration`. The fill/foam RepNotify handlers refresh presentation.

The manager sends ordered event updates and unreliable continuous batches containing temperature, sealed pressure and optional thermal strength. The rocket has its own replicated flight snapshot. Cosmetic motion and material objects are local presentation.

## 11. Cleanup and validation

The 17 November manufacturing update built successfully, compiled/saved BP_Cola and BP_ManualColaFillingMachine, and passed 14/14 focused headless automation checks. Eleven tests retained debug/fixture physics warnings. The player hit receiver is also compiled and saved. See [manufacturing verification and editor settings](Cola_Manufacturing_Ownership_2026-11-17.md#verification) for evidence and limits. The cleanup record below describes the earlier Blueprint graph cleanup.

The 2026-11-15 cleanup removed 28 unused Blueprint variables:

```text
Hit Actors; random vector; unstablity level; As WBP Status Bar Circular 1;
Other Actor; Cola level; max cola level; pre velocirty; Stablity tollrance;
Delta Seconds; can be effected by trace; Hit offset Position Point;
Fill amount; Cap; Dynamic material Instance; Pre Frame Location;
Pre Frame Rotation; First Frame; Rotational Velocity; Locaiont velocity;
G Force; G Force Diffrance From Last Frame; Foam Presser; CurrentPortions;
MaxPortions; BaseEmptyBottleCost; TemperatureCelsius; CurrentFillVolume.
```

Removed graphs: uncalled `Is Valid Hit` macro, empty `Fill Bottel Events` and empty `Rocket Projectile Events`. Removed 14 unreachable attach nodes and seven unreachable detach nodes while preserving live native calls/returns.

The reference audit covered 127 project Blueprints and 788 graphs, including three descendants. Matching character/scanner names were verified as their own members. All ten retained graph exports preserve executable flow. Master and descendants compiled with warnings treated as errors. Full exposed component properties and selected native defaults matched before/after.

Backups/evidence: `Saved/BPColaCleanup_20261115_222050/`, including original master/child assets, documentation, graph/node exports, references, cleanup plan/results and configuration snapshots. Only the master asset is saved for cleanup. Validation covers references, compilation, graph equivalence and configuration preservation; it is not a new multiplayer playtest.

## 12. Source map

| Source | Purpose |
|---|---|
| [ColaPropActor.h](../Source/ColossalFizzCo/Public/Props/ColaPropActor.h), [ColaPropActor.cpp](../Source/ColossalFizzCo/Private/Props/ColaPropActor.cpp) | Bottle API, tick, mass, presentation, cap fallback and original launch. |
| [ColaManufacturing.cpp](../Source/ColossalFizzCo/Private/Props/ColaManufacturing.cpp) | Bottle-owned content acceptance, temporary seal, cap closure/leakage and manufacturing failure policy. |
| [ColaContainment.cpp](../Source/ColossalFizzCo/Private/Props/ColaContainment.cpp) | Filling, impacts, failures, sale quote and pool reset/demotion. |
| [ColaPressure.cpp](../Source/ColossalFizzCo/Private/Props/ColaPressure.cpp) | Plastic agitation, pressure publication, venting and discharge. |
| [ColaLiquidMotion.h](../Source/ColossalFizzCo/Public/Props/ColaLiquidMotion.h), [ColaLiquidMotion.cpp](../Source/ColossalFizzCo/Private/Props/ColaLiquidMotion.cpp) | Cosmetic liquid solver. |
| [InteractableProp.h](../Source/ColossalFizzCo/Public/Props/InteractableProp.h), [ActivePropState.cpp](../Source/ColossalFizzCo/Private/Props/ActivePropState.cpp) | Base actor interface and canonical-state mirrors. |
| [InteractablePropDatabase.h](../Source/ColossalFizzCo/Public/Props/InteractablePropDatabase.h) | Shared definitions and instance/event/continuous structs. |
| [PropAssembly.cpp](../Source/ColossalFizzCo/Private/Props/PropAssembly.cpp), [PropCapAssembly.cpp](../Source/ColossalFizzCo/Private/Props/PropCapAssembly.cpp) | Modular bottle/liquid/cap relationships and presentation. |
| [PropLiquidTransfer.cpp](../Source/ColossalFizzCo/Private/Props/PropLiquidTransfer.cpp), [PropLiquidRelease.cpp](../Source/ColossalFizzCo/Private/Props/PropLiquidRelease.cpp) | Conserved filling and released-liquid accounting. |
| [PropBatchUpdateComponent.cpp](../Source/ColossalFizzCo/Private/Components/PropBatchUpdateComponent.cpp) | Thermal integration, damage and expiry. |
| [StaticMeshPhysicsSimulationComponent.cpp](../Source/ColossalFizzCo/Private/Components/StaticMeshPhysicsSimulationComponent.cpp), [RocketProjectileComponent.h](../Source/ColossalFizzCo/Public/Components/RocketProjectileComponent.h) | Physical settling and rocket state/flight configuration. |
| [RocketLifecycle.cpp](../Source/ColossalFizzCo/Private/Components/RocketLifecycle.cpp), [RocketPoweredMotion.cpp](../Source/ColossalFizzCo/Private/Components/RocketPoweredMotion.cpp), [RocketPressurePhysics.cpp](../Source/ColossalFizzCo/Private/Components/RocketPressurePhysics.cpp) | Rocket handoff policy, powered-distance/curve control and pressure-flight transitions. |
| [ColaPricingLibrary.cpp](../Source/ColossalFizzCo/Private/Props/ColaPricingLibrary.cpp), [ShopProductQuery.cpp](../Source/ColossalFizzCo/Private/Shop/ShopProductQuery.cpp) | Shared pricing and scanner query. |
