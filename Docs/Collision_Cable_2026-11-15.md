# Collision cable implementation — 15 November 2026

## Delivered

- Native, Blueprint-spawnable `UCollisionCableComponent`, displayed as **Collision Cable** in Add Component.
- A compiled separate actor Blueprint: `/Game/Assest/Blueprints/BP_Cable_CollisionSafe`.
- Editor migration utility and `Scripts/CreateCollisionCableVariant.py` to reproduce the variant without overwriting an existing asset.
- Native regression tests under `ColossalFizzCo.Cable`.

The original `BP_Cable`, its placed instances, engine installation, and existing gameplay graphs were not edited. Original asset SHA256 before and after creation:

`723eb08bad4fc86a662d68e0d9912290216010e6a723962c332096c3e28e3bc4`

## Source investigation and confidence

The installed engine is **UE 5.8.2, CL 56702186**. Its installed CableComponent header is present at:

`C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Classes/CableComponent.h`

The implementation file `Private/CableComponent.cpp` is absent from this installation. No claim is made that every internal implementation detail below has been verified against this exact 5.8.2 binary.

The installed header explicitly describes collision as experimental particle sweeps, and declares the particles, integrator, distance solver and collision methods **private and nonvirtual**. A normal subclass therefore cannot replace the solver or write its particle array. Merely adding a second method called `GetCableParticleLocations` would also leave existing stock Blueprint calls reading the stock simulation.

Epic's [current cable documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/cable-components-in-unreal-engine) confirms the particle-based model and per-particle collision sweeps. For implementation-level analysis, the [published Epic-authored UE 5.3 implementation mirror](https://raw.githubusercontent.com/chenyong2github/UnrealEngine/5.3/Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp) was inspected; this is an older version, not the missing 5.8.2 implementation.

### Why the stock approach can clip

1. **Gaps between collision samples.** This Blueprint has 1600 cm length, 32 segments and 10 cm width: nominal particles are 50 cm apart, but particle collision spheres are only 10 cm across. A narrow obstacle can intersect the visible span while neither particle touches it. More solver iterations do not add collision samples between particles.
2. **Single collision pass and penetration recovery in the older source.** The 5.3 code integrates, performs all distance/stiffness iterations, then sweeps each free particle once. Initial penetration uses the sweep's start-penetration displacement on the predicted particle position. It neither resolves the whole segment nor iterates all contacts to convergence. Multiple surfaces, tight corners, and initially embedded particles can therefore remain problematic. This order and exact recovery formula are confirmed only in the older source.
3. **Pinned ends bypass collision in the older implementation.** A moving attachment can place an endpoint across a surface. The rest of the rope is then pulled towards that endpoint. The new solver explicitly sweeps anchor motion.
4. **The Blueprint shuts cable simulation off.** `EventOnGoingIntraction` ends in a retriggerable five-second delay and a request to disable cable ticking. A frozen simulation cannot respond when another object later moves into the cable.
5. **Visual geometry is not automatically collision geometry.** Disabled queries, ignored channels, missing simple collision, and one-sided surfaces approached from their back remain independent scene-authoring problems.

## New component behavior

The component derives from `UMeshComponent` and owns both its simulation and tube renderer. It does not patch or access private engine memory.

- Offers **Collision Method**: **Line Traces (Fast)** or **Collision Sweeps (Thickness)**. Line traces are the default.
- Both methods share **Collision Points Per Frame**, default 8, clamped to **1 through `NumSegments + 1`**. With 32 segments there are 33 points, including both endpoints.
- Processes exactly the configured number of distinct points once per component tick, rotating through the cable across frames. A selected point also checks its adjoining span; the last point uses the preceding span. Spans shared by selected points are queried once per batch.
- Line mode traces historical point/midpoint motion and the selected span's centreline. Contacts offset the rope by half its width plus Collision Skin. This approximates thickness; it does not provide the volume coverage of a sweep.
- Sweep mode uses particle and midpoint sphere sweeps plus selected-span capsule penetration recovery. Recovery iterations are restricted to the selected spans.
- Caches contact planes and projects them using arithmetic during simulation substeps and distance/stiffness solving. No world collision queries run inside those loops. Increasing substeps or solver iterations does not multiply the point budget.
- Generated seeds use floor-aware initialization casts. Immediate recovery mode performs a one-time full-path recovery at reset; smooth recovery instead eases embedded points out through budgeted ticks. `Is Cable Collision Free` remains an explicit, potentially expensive full-capsule diagnostic; ordinary ticking does not call it.
- Applies contact velocity correction, tangential friction and damping.
- Interpolates attachment motion across bounded substeps and sweeps the endpoints. Collision can cause an endpoint to lag its requested socket.
- Defaults to Post Physics, a maximum 1/120-second step and eight substeps. Hitches drop excess simulation time instead of integrating with a huge step.
- Keeps collision ticking despite the legacy Blueprint sleep request. Disable `Keep Collision Awake` if an explicit tick shutdown is required.
- Replicates endpoint component, socket, offset and attachment flags. Particle simulation is local to each machine; this is not authoritative network rope physics.
- Exposes `Has Unresolved Collision`, `Attachment Blocked`, `Dropped Simulation Time`, `Collision Query Count`, `Last Collision Point Indices`, and `Last Simulation Time Ms`. Ordinary-tick query count includes the batch's collision queries. Initial reset work and explicit full-path diagnostics are separate. With a partial budget, the flags only describe known/checked contacts, not a certified full-path result.
- Supports the stock-style endpoint attachment, particle-location and cable socket APIs. Width and collision radius use world centimetres, including under scaled parents.

## Using it

1. **Restart the open editor before opening the new Blueprint.** The running session has the old native DLL loaded. Separately named DLLs were built successfully so that the session could stay open. The current project module manifest points to the new build for the next launch.
2. Open `/Game/Assest/Blueprints/BP_Cable_CollisionSafe` and select its **Cable** component. Existing `BP_Cable` instances keep using the original class; existing `BP_Cable_CollisionSafe` instances use this native component.
3. For a new actor, add **Collision Cable** from Add Component.
4. Keep `Enable Collision` on. Use blocking query collision on the relevant floors and walls. Defaults use simple collision; enable `Trace Complex` only when triangle queries are needed, and assess their cost.
5. Choose **Collision Method** and set **Collision Points Per Frame**. `1` checks one rotating point and its adjoining span per frame. Set it to `NumSegments + 1` to check the complete cable each frame. Both methods use this same setting.
6. Start with anchors outside collision geometry. For an initial route around an obstacle, provide a clear world-space path with `Set Cable Particle Locations` (exactly `NumSegments + 1` points). A straight line initialized through a solid does not encode which way to route around it.

The variant retains the original interaction, braking, gravity and tension-helper graphs. Those helpers' pre-existing arithmetic and gameplay behavior were not repaired as part of this collision change. The component's name is still `Cable`, but its class and graph member/function references have been retargeted to the custom component. It is not a subclass of `UCableComponent`; external Blueprints whose inputs explicitly require the stock class must likewise be retargeted.

The initial variant copied the original 1600 cm length, 32 segments, 10 cm width and attachment offsets. Subsequent user-authored Blueprint/level settings are preserved by the native performance update.

## Validation

The original sweep implementation passed these seven regressions; the subsequent placement repair added two more. Reports below retain that history. The budgeted implementation is rechecked separately under `Saved/CableBudgetValidation`.

| Test | What it verifies |
| --- | --- |
| FloorAndRestingContact | 32-segment rope falls and rests above a solid floor; all spans checked over 120 frames; hitch steps bounded; legacy sleep request prevented |
| ThinWallHighSpeed | Fast motion under 5,000,000 cm/s² acceleration does not cross a 1 cm wall |
| ObstacleBetweenParticles | A 2 cm obstacle blocks the middle of a 100 cm span even when neither endpoint touches it |
| AttachmentMotionAndMountFiltering | Fast pinned-end motion stops at a wall; a wall sharing the attachment actor is still considered; socket reports actual endpoint |
| PenetrationRecoveryAndImpossibleAnchors | Embedded free rope recovers; impossible pinned endpoints are reported |
| MovingObstacleRecovery | A box moved into a clear rope causes contact recovery; an invalid historical path is not reused |
| TriangleSurface | Falling rope remains above a triangle-mesh plane with complex queries enabled |

Results: `Saved/CableValidation/Report/index.json` and `Saved/CableValidation/Tests.log`.

Variant creation, compilation and a spawn/component inspection passed: `Saved/CableValidation/Variant.json`. The saved asset contains one `CollisionCableComponent` and no stock `CableComponent`.

Tests used an isolated headless editor and user directory. They are collision regressions, not visual QA, a packaged-build check, multiplayer validation, or exhaustive testing of the user's actual level/landscape.

## Practical limits

Lower budgets trade collision response for processing cost. A 33-point cable at budget 1 revisits each point every 33 frames (about 0.55 seconds at 60 FPS); moving geometry or fast rope motion can intersect unchecked spans during that interval. Cached contacts continue to support known surfaces, but a removed or moved surface is only refreshed when its point/span is selected. Even maximum budget does not make line traces equivalent to volume sweeps, nor guarantee arbitrary fast rotations or impossible geometry can be solved.

A collider that completely teleports past the rope between scene queries, anchors embedded in solids, squeezed configurations with no free space, missing collision, or the back side of a one-sided surface still require scene/gameplay handling. `Has Unresolved Collision` reports known configurations the finite recovery budget cannot clear. Endpoint blocking does not physically stop the player; the original Blueprint still handles character movement.

There is no rope self-collision, knot solver, rigid-body impulse transfer or automatic pathfinding around an initially intersected wall. Favoring collision can allow stretch or block anchor motion when cable length and routing cannot both be satisfied.

The previous unbudgeted implementation performed collision inside every constraint correction and used 4,428 motion/recovery queries on the final frame of the extreme eight-segment wall test. That implementation has been replaced by the single batch per tick. A point budget is not an exact query budget: each selected point may need motion, span, contact refresh, and bounded recovery queries. All particles still receive gravity/length simulation and rendering updates. Profile the actual number of ropes and choose a budget appropriate to their speed and scene complexity.

## Placement repair — 15 November 2026

The first implementation missed editor lifecycle handling. It retained particle positions in world space across actor/component moves and same-sized re-registration. A component moved far from the origin could therefore have its endpoints pulled towards the newly placed actor while the middle remained at the previous location. The original seven tests did not cover this; two additional regression tests reproduced the failure before repair.

An additional nested-component bug was found: `FComponentReference::GetComponent(GetOwner())` resolves an empty reference to the actor root. For an unconfigured cable target, the documented offset must instead be relative to the cable component. The component now explicitly distinguishes an empty reference from an explicit actor-root attachment.

The repair:

- Enables `OnUpdateTransform` notifications and immediately reinitializes editor and pre-play placements, including nested component transforms, without requiring a realtime viewport tick.
- Reinitializes editor registration even when segment count has not changed, and handles edited properties.
- Preserves world-space particle history and swept anchors after gameplay has begun.
- Seeds slack as a gravity-directed, arc-length-sampled curve; coincident endpoints produce a hanging loop instead of a collapsed set of particles with arbitrary expansion directions.
- Sweeps generated starting points from their mount against blocking geometry so a hanging loop near a thin floor starts on the accessible side. Explicit user-supplied particle paths are not modified by this seed-generation step.
- Correctly resolves unspecified targets relative to the cable component, and clears obsolete component paths when setting a new attachment.

`EditorPlacement` covers both editor and Blueprint-preview worlds, far-origin placement, rotation, non-uniform scale, nested-component moves, and re-registration. `CoincidentEndpointInitialization` covers the zero-offset, both-ends-attached configuration with stiffness enabled. Runtime fixtures now explicitly begin play so the existing fast-anchor regression tests continue to exercise gameplay movement rather than editor placement behavior.

Placement repair reports are in `Saved/CablePlacementValidation`. `BeforeReport` records the two expected regression failures on the old implementation. `Scripts/ValidateCollisionCablePlacement.py` checks a transient instance of the saved Blueprint without saving assets or maps.

Final validation: C++ build succeeded; all **nine** cable tests passed with zero failures/warnings (`FinalReport/index.json`). The coincident-endpoint test additionally exercises a 500 cm loop mounted 65 cm above a blocking floor. A focused test first exposed a below-floor seed and passed after the floor-aware seed correction. The saved Blueprint also passed transient far-origin spawn, translation/rotation/scale, and endpoint-edit checks (`BlueprintPlacement.json`). Its saved asset hash remained unchanged. These are headless state/collision checks, not a rendered visual comparison of the user's level.

The native repair requires restarting the open editor. Save the current unsaved Blueprint/map changes before restarting. No Blueprint settings or level assets were overwritten by the repair. With both attachment flags enabled and an empty target plus zero End Location, both ends correctly share the cable component's location; use End Location to author a separated far endpoint.

## Shared per-frame collision budget — 15 November 2026

The expensive collision queries inside the integrator and constraint solver have been replaced with a single rotating collision batch shared by both selectable methods. Cached planes support known surfaces between queries. Recovery reuses the established side of a collider on adjacent samples so independent penetration normals do not push neighboring points to opposite sides. Refresh probes reach the cached surface even when the rope has lifted away from it.

Development Editor/UHT build succeeded. All **12** focused cable tests passed with **zero test failures or warnings**, in `Saved/CableBudgetValidation/FinalReport/index.json` and `FinalTests.log`. The original nine regressions use full-budget sweep mode for strict whole-cable collision assertions. New coverage:

- `SharedPerFrameBudget`: both methods at budgets 1, 3, and all 9 points; exact distinct-point counts and continuous rotation; clamping to 1..actual point count; no collision work when disabled; substeps and solver iterations do not multiply collision world queries.
- `LineTraceFloorAndWall`: full-budget line mode falling/resting on a floor and fast motion against a thin wall.
- `LowBudgetHangingLoop`: both methods at budget 1 with a hanging loop in clear space, and previously established floor contacts remaining valid through lifting, falling and runtime method switches between point checks. Switching methods preserves the cursor and shared contact history. This does not assert that budget 1 can detect every new contact immediately.

In the extreme eight-segment high-speed wall regression, the final sweep implementation used **42 queries** on the final frame, compared with **4,428** in the previous unbudgeted implementation (about 99% fewer world queries in that test). This is a query-count comparison, not a measured FPS improvement. In the static empty-scene budget test, line mode used 1 query at budget 1 and 8 at full 9-point budget; sweep mode used 3 and 25 respectively. A selected point can require several queries for its motion, neighboring span and recovery.

The new properties default to line traces and an 8-point budget. The currently open editor still needs a restart to load the new native class layout and expose them. The project module manifest points to build `15159`. No saved Blueprint or map was rewritten for this update.

A transient-instance check of the user's saved Blueprint also passed: spawn/placement transforms, endpoint edits, and editor budget clamps. Its current saved configuration has **64 segments / 65 points**, so its range is **1..65**, with the new default budget 8 and line-trace method. The saved asset hash was unchanged by validation. Details: `Saved/CableBudgetValidation/BlueprintPlacement.json`.

## Subsequent level test

The subsequent saved asset snapshot had 32 segments / 33 points. Both methods hung down but **failed** new-contact floor/wall tests at budget 1 in the saved validation level. All-point sweeps passed the controls; all-point line traces still showed small penetrations. The earlier unit tests of low-budget *known* contacts did not certify detection of new contacts on unchecked points. See [the full level-test measurements](Cable_One_Point_Level_Test_2026-11-15.md).

## Smooth recovery and player filtering

The next revision adds gradual, depth-dependent bounds-center push-out, a shared per-point correction speed cap, a surface-normal fallback for unsuitable center directions, and player collision filtering. The three physical mesh parts of `BP_Cable_CollisionSafe` also ignore Pawn. Native build `15160` and all 15 cable tests passed; see [the recovery controls and validation details](Cable_Smooth_Recovery_2026-11-16.md). These defaults permit temporary penetration while easing out and supersede the immediate behavior described in the earlier implementation history.
