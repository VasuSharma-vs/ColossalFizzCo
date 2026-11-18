# Cable rendering and collision quality — 17 November 2026

`/Game/Assest/Blueprints/BP_Cable_CollisionSafe` uses the updated native `CollisionCableComponent`. The original Blueprint remains separate. No source asset needs to be resaved to pick up the new property defaults.

## Rounded subdivision and outward faces

The tube's triangle order exposed its inside faces. The renderer now uses Unreal's exterior face winding, outward radial shading normals, and a texture V tangent consistent with the UV direction.

Subdivision previously added samples but limited their departure from each straight simulation span to the smaller of **Max Visual Deviation** and 80% of the collision skin. With a 0.5 cm skin, the curve could bow only 0.4 cm, leaving large bends visibly angular. That forced skin limit is removed.

Under **Cable Rendering**:

| Control | Behavior |
|---|---|
| Enable Visual Subdivision | Enables additional lengthwise rendering samples. |
| Visual Subdivisions | 1–8 samples per simulation span; try 4–8 for visibly rounded bends. Default 2. |
| Visual Smoothing | 0 produces straight spans; 1 produces the full interpolated curve. Default 1. |
| Limit Visual Deviation | Optional cap on how far the curve bows from each straight span. Default off. |
| Max Visual Deviation | Cap in cm, used only when the preceding switch is enabled. |

The curve passes through the simulation points and smoothly joins their neighboring spans. Twelve simulation points produce 89 render samples at eight subdivisions. The simulation point count and cross-section side count stay unchanged. Render bounds include the rounded curve, and longitudinal UVs follow its distance.

## Full collision and CPU controls

Under **Cable → Collision**, **Full Collision Queries (High CPU)** selects the stronger swept-query path. It is off by default so existing budgeted cables retain their cost settings.

When enabled, the component:

- Sweeps a sphere with cable thickness along point integration and constraint motion, including a checked slide after a hit.
- Checks every awake point and simulation span during solver passes, including attachment safety checks. It uses sweeps regardless of the stored line-trace selection.
- Checks the rendered curve with capsule queries. If a rounded span intersects a blocker, it reduces that span's bow toward its simulated chord without moving its simulation knots.
- Reuses the validated render path for getters and additional views. Fully sleeping, unchanged cables retain their cached mesh; wake monitoring remains active.

**Full Collision Solver Interval** reduces full-mode query work: 1 checks span collisions each solver iteration, while 4 or 8 checks less often. The final solver pass and point-motion sweeps remain enabled. Increasing this interval trades intermediate constraint-contact accuracy for fewer queries; it does not turn full mode into a one-point-per-frame mode. More simulation segments, solver iterations, substeps, and visual subdivisions still increase its cost.

For the lowest query cost, turn **Full Collision Queries** off, select **Line Traces** or **Sweeps**, and set **Collision Points Per Frame** from 1 up to the cable's point count. Both methods retain their rotating per-frame budget. Full mode preserves these settings and restores their use when switched off.

**Collision Query Count** reports simulation collision queries. **Render Collision Query Count** separately reports curve-validation shape queries. **Wake Query Count** reports the sleeping wake monitor. These counts measure query work, not milliseconds or frame rate.

Cached contact planes also guard rounded samples in budgeted modes without additional queries, but they cannot discover a new obstacle that the sparse simulation checks have missed.

## Validation and limits

The native suite passed **36/36 tests, zero failures** in the isolated project snapshot (`Saved/CableRenderFixValidation/Report/index.json`). The added checks verify actual mesh face winding and UV tangent handedness, rounded joins and curve bounds, unchanged simulation resolution, all-point full mode versus the one-point budget, reduced query count with a larger full solver interval, high-speed motion against a thin wall, and protection of a rounded visual span from a small obstacle. Repeated render getters issue no additional collision queries.

The separate DX11 rendered check passed with a one-sided material: the front surface's measured world normal was approximately `(0.007, -1.000, -0.024)`, pointing toward the camera. The exported comparison visibly rounds the same twelve-point path at eight subdivisions (89 render samples), alongside the unsmoothed twelve-sample path. In that camera view, the smooth cable is on the left and the polygonal cable is on the right. The saved custom Blueprint's file hash remained unchanged.

The main Development Editor build succeeded, producing `UnrealEditor-ColossalFizzCo-15202.dll`. All 17 cable source files match the isolated tested snapshot, with no source changes after those tests. See `Saved/CableRenderFixValidation/VerifiedSummary.json` and `SourceProof.json`.

The rendered check and build evidence are recorded in `Saved/CableRenderFixValidation`. Its fixtures use the saved custom Blueprint, remain unsaved, and run in a separate helper process. The user's open Unreal editor is not closed or restarted. New reflected controls become available when that editor next loads the rebuilt native module.

Full mode offers stronger collision handling, not an unconditional no-clipping guarantee. Geometry must have query-enabled blocking collision. An already embedded point still uses the configured gradual recovery. Embedded fixed anchors, incompatible length constraints, very sparse simulation spans, and moving geometry can require more resolution or different settings. Reducing full-mode checks or using a one-point budget increases the chance of missed contacts. Rendering cannot repair a physical span that already intersects a wall.
