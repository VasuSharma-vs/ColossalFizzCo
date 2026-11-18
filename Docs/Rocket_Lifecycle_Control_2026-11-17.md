# Rocket lifecycle control

Updated 17 November 2026. Editor build **15703** succeeds, BP_Cola's preset is saved and verified, and **46/46** selected native regressions pass (31 clean, 15 with warnings). Restart the editor to load the new native controls. Rendered gameplay feel and a real remote-client run remain unverified for this update.

## Designer settings

Open `/Game/Props/Colas/BP_Cola`, select `RocketProjectile`, and edit **Falling Settings** and **Powered Motion Settings**. The BP_Cola preset selects `AfterBounceApex`, `HandoffAfterBounces = 1`, and enables the lifecycle speed curve. Its intended sequence is:

```text
Tipping -> dragging -> powered flight -> low-fuel climb
        -> scripted fall -> one scripted floor bounce
        -> rebound apex -> Chaos physics -> supported rest
```

The count controls scripted bounces before physics takes over. Subsequent physical contact responds to the body's collision settings and damping. The planned rebound uses the curve's actual vertical maximum, including when the two ground points have different heights.

### Choosing when physics takes over

`FallingSettings.PhysicsHandoffMode` has four choices:

| Choice | Behavior |
| --- | --- |
| `UseLegacySettings` / Use Existing Handoff Switches | Uses immediate physics when `FallingSettings.bUsePhysicsSimulation`, `FallingSettings.bHandoffToPhysicsOnDepleted`, or the component's legacy `bHandoffToPhysicsOnDepleted` is true. When all three are false, falling remains scripted. |
| `Disabled` / Keep Scripted Falling | Keeps falling and landing under the scripted controller, regardless of those older switches. |
| `OnFuelDepleted` / Physics When Fuel Ends | Transfers to Chaos when powered movement ends. |
| `AfterBounceApex` / Physics At Rebound Apex | Keeps the initial fall and the requested `HandoffAfterBounces` under scripted control, then transfers at the rebound apex. |

Explicit enum choices take precedence over the old switches. Disabling legacy physics handoff is therefore respected. `HandoffAfterBounces` defaults to 1; flight ricochet limits and ordinary falling-bounce settings remain separately editable.

### One speed curve across powered stages

Enable `PoweredMotionSettings.bUseLifecycleSpeedCurve` to use its inline `SpeedCurve`. X is lifecycle progress, not elapsed seconds. Y is a multiplier of the launch's base speed.

| Stage | Curve X range |
| --- | --- |
| Tipping | 0–0.1 |
| Dragging | 0.1–0.25 |
| Main flight | 0.25–0.8 |
| Low-fuel climb | 0.8–1 |

Default linear curve keys are `(0, 0.04)`, `(0.1, 0.2)`, `(0.25, 0.45)`, `(0.45, 1)`, `(0.8, 0.4)`, `(1, 0.03)`. They produce a slow start, acceleration into flight, and deceleration toward the final climb's apex. Curve progress never rewinds when a phase changes.

`PoweredMotionSettings.Acceleration` defaults to 2,200 cm/s² and `Deceleration` to 1,800 cm/s². These limit how quickly speed approaches the curve target. Disable the curve to retain the component's existing phase-specific speed behavior. The native default is disabled; BP_Cola's preset enables it.

During tipping the curve also scales rotational phase progress, with a minimum progress rate of 0.1 to prevent a zero key from trapping the bottle in tipping. Drag duration remains independently authored; the curve controls movement speed within that duration. Liftoff retains the accumulated speed and rebases the first path node to the actual takeoff position.

### Falling speed and settling

Both the predicted flight-plan fall and the runtime ballistic fall use these authored settings:

| Setting | Effect |
| --- | --- |
| `FallAcceleration` | Base downward acceleration; default 3,000 cm/s². |
| `SpentGravityScale` | Multiplies falling acceleration; default 1. |
| `FallingSpeedMultiplier` | Scales scripted descent acceleration and speed; default 1. |
| `TerminalVelocity` | Base descent-speed cap; default 2,600 cm/s. |
| `ApexHangDuration` | Brief pause at the planned initial apex; default 0.12 seconds. |

Effective falling acceleration is `FallAcceleration × SpentGravityScale × FallingSpeedMultiplier`; the descent cap is `TerminalVelocity × FallingSpeedMultiplier`, subject to native input clamps. On the planned fall these govern path traversal; on a ballistic fall they govern downward velocity. These controls govern scripted falling. Once physics takes over, Chaos gravity and the body's physical settings govern motion.

At handoff, linear damping defaults to **2.5** and angular damping to **5**. The rocket checks for speed below **80 cm/s** and rotation below **60 deg/s**, plus a supporting surface within 2 cm beneath the body's bounds, for `AtRestSettings.SettlingLockDuration` before reporting rest. Low speed in mid-air alone cannot settle the bottle. Handoff damping is temporary and the saved body settings are restored when the flight finishes or resets.

## Native ownership and compatibility fixes

`AColaPropActor::LaunchOriginalBottleRocket()` no longer overwrites authored bounce limits with eight flight ricochets and four falling bounces. It still supplies the bottle's seeded launch direction, distance/speed range and erratic-flight setup.

`HandleRocketFuelDepleted()` retains liquid release, pressure clearing, mass/material refresh and the unsettled-timer request. It no longer calls `FinishPressureThrust()` recursively to force physics. The unsettled request respects the bottle's active motion lock.

For scripted launches, `FinishPressureThrust()` delegates to the idempotent `EndScriptedThrust()` policy. It does not turn on the pressure-physics flag or change replication into physics mode merely because the bottle is empty. An external cutoff begins the fall at the current pose and momentum instead of jumping to a future planned apex. The separate Chaos pressure-thrust mode continues using its existing physical body.

`GetPoweredTravelDistance()` returns the complete planned powered distance through the low-fuel climb, with the configured fuel distance as fallback. Both the component cutoff and `UpdateOriginalRocketDischarge()` use this distance. The nominal main-flight distance can no longer empty the bottle before its appended climb finishes. The Prop Manager still accounts for released portions under the bottle's `PressureReleaseEpisode`; depletion releases any remaining contents once.

Scripted movement keeps physics disabled. At the chosen handoff the existing pose and momentum are passed to the physical body; rebound-apex handoff has zero vertical velocity. The server controls transitions and final settling, with clients following replicated state.

`UStaticMeshPhysicsSimulationComponent` now recognizes both powered and spent scripted movement, so an unsettled request cannot enable physics during the scripted fall. Once the rocket transfers to physics, the helper preserves actor movement replication through subsequent settling state changes. The handoff also reapplies replication ownership after prop lifecycle callbacks.

## Source and validation

Configuration is declared in [RocketProjectileComponent.h](../Source/ColossalFizzCo/Public/Components/RocketProjectileComponent.h). Policy and powered-distance logic are in [RocketLifecycle.cpp](../Source/ColossalFizzCo/Private/Components/RocketLifecycle.cpp); the curve and acceleration limiter are in [RocketPoweredMotion.cpp](../Source/ColossalFizzCo/Private/Components/RocketPoweredMotion.cpp). [RocketProjectileComponent.cpp](../Source/ColossalFizzCo/Private/Components/RocketProjectileComponent.cpp) owns planned/ballistic falling, rebound and physical settling. Bottle integration remains in [ColaPropActor.cpp](../Source/ColossalFizzCo/Private/Props/ColaPropActor.cpp), [ColaPressure.cpp](../Source/ColossalFizzCo/Private/Props/ColaPressure.cpp) and [RocketPressurePhysics.cpp](../Source/ColossalFizzCo/Private/Components/RocketPressurePhysics.cpp).

Validation on Editor **15703**:

- `ColossalFizzCo.Props+ColossalFizzCo.Shop.ManualSoloFilling`: **46/46 pass**, no failures or unrun cases; 15 tests emit warnings, including non-simulating-body mass queries, collision configuration and animation Blueprint threading warnings.
- Six `Props.RocketLifecycle` tests cover explicit/legacy depletion policy, preserved pose and momentum, damping restoration, supported rest, planned versus interrupted fall, first-bounce apex timing on level and unequal-height ground, all four powered curve stages, zero-curve timeout, falling acceleration/caps, and saved BP_Cola settings/curve keys.
- Existing bottle tests retain conservation, physical handoff and client-snapshot assertions. The tests that specifically require immediate physics now explicitly select `OnFuelDepleted`; the saved bottle preset selects `AfterBounceApex`.
- The authored component was configured and saved in a separate editor commandlet; the saved curve and handoff values are loaded by the native asset test. Existing tipping, dragging, flight and low-fuel settings were preserved.

Evidence: `Saved/RocketLifecycle_20261117/Build.log`, `AssetConfiguration.json`, `Tests.log` and `Report/index.json`. The pre-change Blueprint backup is `Before/BP_Cola.uasset` under that directory. The currently open editor still needs a restart; avoid saving its stale BP_Cola component layout over the saved preset. Tests used transient headless worlds; rendered gameplay feel, real remote-client behavior and packaging were not validated in this update.
