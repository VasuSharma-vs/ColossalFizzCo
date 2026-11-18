# Cable smooth recovery and player filtering

Implemented for `BP_Cable_CollisionSafe` through `UCollisionCableComponent`. The user's open editor was left running; the native changes are built as module suffix `15160` for the next editor launch. Its current session still has `15159` loaded, so the new recovery controls require a later restart when the user is ready. No restart or live native-class reinstancing was forced.

## Recovery controls

Under **Cable → Collision → Recovery**:

| Setting | Default | Behavior |
| --- | ---: | --- |
| Smooth Collision Recovery | On | Gradual correction of existing penetration; Off restores immediate correction |
| Use Bounds Center Recovery | On | Uses point position minus the hit component's bounds center as the outward direction |
| Collision Recovery Rate | 8 / s | Deeper penetration increases correction speed |
| Min Collision Recovery Speed | 5 cm/s | Allows small remaining overlaps to finish clearing |
| Max Collision Recovery Speed | 100 cm/s | Caps each point's total collision correction across the whole component tick |

Both line traces and sweeps share these controls and the existing **Collision Points Per Frame** budget. A selected point can require several queries for motion, span checks and penetration recovery. Cached contacts continue moving recovering points between their query turns, without new world collision queries for those points.

The direction is computed from the **hit primitive component's** world bounds center, not the owner's actor origin. Actual surface contacts provide the depth. A zero direction, an inward direction, or a direction too parallel to the surface falls back to the surface normal. This prevents a very wide floor from pulling the cable sideways simply because its bounds center is far away.

When a query begins inside geometry, a bounds-guided trace from outside the component towards the point can recover an actual surface contact. This also supplies a runtime recovery path for inside-start line traces. Sweeps retain their penetration-depth fallback if the surface probe cannot produce a usable hit.

Push-out speed scales with the required correction distance, clamped between the minimum and maximum. The distance allowance is shared by all contacts, solver iterations and substeps for that point during the tick. Cached contacts prevent gravity, attachment motion and rope-length corrections from deepening an already known overlap while it recovers. Recovery displacement and its transmitted outward correction are excluded from launch velocity.

Explicit particle resets no longer perform an immediate full-capsule push-out when smooth recovery is enabled. Generated sag/loop seeds still use the floor-aware initialization casts. With smooth recovery disabled, the previous immediate initialization recovery remains available.

`Has Unresolved Collision` remains true while a known contact is still penetrating. `Max Recovery Distance This Frame` exposes the largest cumulative collision correction applied to a point that tick. This limit applies to collision repair; regular forces and rope motion can move points farther.

Gradual recovery deliberately permits a temporary overlap. A low query budget still delays discovering new contacts. The method does not guarantee immediate no-clipping behavior or solve arbitrary concave geometry and impossible anchors.

## Player collision

**Ignore Player Collision** defaults to On for the cable solver. It ignores the Pawn collision channel and explicitly ignores player-controlled pawn actors so a player capsule or mesh using another object channel is also excluded. Cached ignored-player contacts are removed when the filter is enabled. Ignoring the Pawn channel also ignores AI pawns that share that channel.

The Blueprint's `Cube`, `Cube1` and `Cylinder` component templates originally used `BlockAllDynamic`, which still blocked player movement independently of the cable solver. Their Pawn response is now **Ignore**. Their collision remains `QueryAndPhysics`, with the original object type, simulation state and other body settings preserved. The Blueprint was edited through the already-loaded editor objects, compiled without warnings and saved individually, preserving its existing in-memory edits. No other asset or level was saved by this change.

The mesh-component Pawn responses are separate from the solver's Ignore Player Collision option. Re-enabling player contact on those physical parts requires changing their Pawn response back to Block.

Pre-change saved asset and body-instance snapshots are under `Saved/CableSmoothRecoveryValidation/Backup`.

## Validation

Development Editor C++/UHT build succeeded. All **15 cable automation tests passed with zero failures or warnings**:

- The existing 12 collision, placement, budget and cached-contact regressions exercise the immediate option where strict no-penetration checks are required.
- `SmoothBoundsRecovery` exercises both methods: no initial teleport, outward bounds-center correction, greater correction for deeper overlap, per-frame speed limits under varying substeps/iterations, one-point scheduling, eventual clearance, no artificial launch, and a finite fallback at the exact bounds center.
- `SmoothFloorRecovery` checks settling on a broad floor, preserving clearance without sideways bounds-center drift.
- `IgnorePlayerCollision` checks default Pawn exclusion, re-enabling contacts, player-controlled pawns on a custom object channel, clearing ignored cached contacts, and retaining world-wall collision.

Reports: `Saved/CableSmoothRecoveryValidation/RetestReport/index.json` and `Retest.log`. The first report records the two issues found before the final correction: recovery-induced launch velocity and an incomplete player-state fixture.

`Scripts/ValidateCableRecoveryDefaults.py` passed in a fresh headless load: all three recovery/player-filter booleans are enabled; rate/min/max speeds are 8 / 5 / 100; all three spawned meshes ignore Pawn and still block WorldStatic, WorldDynamic and PhysicsBody. The saved asset was unchanged by validation. Its report is `Saved/CableSmoothRecoveryValidation/BlueprintDefaults.json`. That saved snapshot has 32 segments / 33 points and an 8-point query budget.
