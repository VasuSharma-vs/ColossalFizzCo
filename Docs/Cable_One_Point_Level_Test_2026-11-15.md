# Cable one-point level test — 15 November 2026

**Result: both methods hang down, but neither is collision safe at one point per frame. Full-point sweeps passed the same floor/wall controls.**

The user's open Unreal Editor was left running. Its log confirmed native build `15159`. Testing used separate, hidden, below-normal-priority editor processes with NullRHI and isolated save directories. No Blueprint or map was saved or rewritten. Both test processes completed and exited.

## Method

- Loaded the saved `/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation` level.
- Spawned four temporary copies of the saved `BP_Cable_CollisionSafe` and temporary blocking floor/wall meshes in that level, then ran Simulate-In-Editor.
- Tested line traces and sweeps sequentially for 300 game frames each, using verified 1/60-second game timesteps.
- Ran a second session using all points as a control. The saved Blueprint instances in this test had **32 segments / 33 points**, 10 cm width, and a 0.5 cm collision skin. This is the saved asset snapshot used for this run, rather than the earlier 65-point snapshot.
- Checked visible-radius floor and wall penetration every frame. Ran the complete capsule diagnostic every fifth frame. Verified the number of selected points against the configured budget; there were no budget failures or harness errors.
- Measured `LastSimulationTimeMs`, which covers the native component tick. External validation queries are outside that timer. NullRHI excludes rendering: these timings are not viewport FPS or GPU measurements. Background application load and scheduling affect timings.

Cases: an 800 cm hanging loop mounted 500 cm above the test floor; a 500 cm loop mounted 65 cm above it; a free vertical rope accelerated towards a wall at 1,000 cm/s² for 80 frames; and an attached rope whose mounting actor moved towards/through that wall at 5 cm per game frame. The wall was 10 cm thick and 600 cm high. The floor top was Z=0.

## One point per frame

| Method | Hanging | Floor penetration frames / 300 | Moving-rope wall penetration frames / 300 | Moving-attachment wall penetration frames / 300 |
| --- | --- | ---: | ---: | ---: |
| Line traces | Pass | 70 | 32 | 28 |
| Sweeps | Pass | 44 | 32 | 29 |

Both floor cases first penetrated on frame 38 and reached about **13.95 cm of visible cable penetration**. The moving-rope wall cases first crossed the visible-radius boundary on frame 101; moving attachments first crossed on frame 99. Recovery later returning the rope to the correct side does not erase those failures.

At 33 points and budget 1, a point receives a new collision check only once every 33 component ticks, about **0.55 game seconds at 60 Hz**. All particles still move under gravity, forces, attachment movement and length constraints during that interval. Cached planes support known contacts, but cannot detect every new contact on unchecked points. A one-point budget is also not a one-query limit: motion, adjoining-span and contact-refresh queries may be needed for the selected point.

## CPU timings

Representative near-floor loop component timings, milliseconds per native tick:

| Budget and method | Median | 95th percentile |
| --- | ---: | ---: |
| 1 point, line traces | 0.0131 | 0.0204 |
| 1 point, sweeps | 0.0130 | 0.0208 |
| All 33 points, line traces | 0.1022 | 0.1672 |
| All 33 points, sweeps | 0.1287 | 0.2383 |

Across the four one-point cases, component medians ranged from about 0.007 to 0.022 ms. These small timing differences do not establish that one query method is always faster than the other. For the full-point sweep wall-motion case, median component cost was 0.1762 ms and the 95th percentile was 0.3094 ms.

## Full-point controls

All-point **sweeps** passed all four cases: no visible floor/wall penetrations and no failed full-capsule diagnostics.

All-point **line traces** passed the free-rope wall case but still showed six floor-penetration frames (worst about 1.14 cm) and one attachment-wall penetration frame (about 0.91 cm). Centreline traces approximate a thick cable; checking every point does not make them equivalent to volume sweeps.

For the tested scene, use **Collision Sweeps + all actual cable points** when surface blocking is the priority. Lower budgets reduce processing and delay collision detection. No finite sample of cases establishes a universal no-clipping guarantee, including for teleporting objects, missing collision, inaccessible initial routes or impossible anchor configurations.

## Reproduction and raw results

- Runner: `Scripts/TestCableOnePointInLevel.py`.
- One-point results: `Saved/CableOnePointLevelValidation/Result.json` and `Editor.log`.
- Full-point controls: `Saved/CableOnePointLevelValidation/FullPointControl/Result.json` and `Editor.log`.
- Add `-CableFullPointControl` to run the all-point control. The script requires `-CableOnePointIsolated`, `-nullrhi`, and a project saved directory inside its test output directory; it refuses to run in the user's interactive session.
- Input Blueprint/map hashes were unchanged during both sessions. Test save files were confined to the isolated `User/Saved` directories.
