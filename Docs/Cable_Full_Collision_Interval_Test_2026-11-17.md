# Full collision interval level test — 17 November 2026

**Intervals 1, 4, and 8 passed all tested floor/wall checks and hung downward. Interval 8 used the least cable CPU time in this scene.**

The user's original Unreal editor remained running and responsive (process 24396). Testing ran in a separate headless editor using the saved `/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation` level and four temporary copies of `/Game/Assest/Blueprints/BP_Cable_CollisionSafe`. The helper finished and exited. Neither the Blueprint nor the map was saved or changed; both file hashes matched before and after the run.

## CPU comparison

Native component time in milliseconds per 60 Hz game frame, summed across the **four active test cables**:

| Full collision solver interval | Median total | 95th percentile total | Median reduction from interval 1 |
|---|---:|---:|---:|
| 1 | 12.38 ms | 17.24 ms | — |
| 4 | 5.70 ms | 7.86 ms | 54% |
| 8 | 4.77 ms | 6.58 ms | 61% |

Median time for each individual cable:

| Scenario | Interval 1 | Interval 4 | Interval 8 |
|---|---:|---:|---:|
| Hanging loop | 1.83 ms | 1.07 ms | 0.96 ms |
| Near-floor loop | 3.32 ms | 1.80 ms | 1.68 ms |
| Free cable accelerated into wall | 4.47 ms | 1.57 ms | 1.07 ms |
| Attachment moved through wall | 2.72 ms | 1.19 ms | 0.98 ms |

These are `LastSimulationTimeMs` measurements: native component work, including the smoothed render-path collision checks and geometry publication. Python validation queries run outside that timer. NullRHI excludes actual GPU drawing; these numbers are neither viewport FPS nor whole-process CPU percentages. They were measured on the Ryzen 7 5700X while the original editor remained open. Other machine activity can affect timing.

Sleeping was deliberately disabled to compare active collision work. A settled cable with sleeping enabled can cost substantially less. Full mode still sweeps individual point motion at every interval; increasing the interval reduces the repeated all-span solver checks. Render-path checks remain enabled. For example, median simulation query counts for the moving-wall cable were 3,366 / 1,128 / 744 per tick for intervals 1 / 4 / 8, respectively; its render-path queries were 128 at each setting.

## Collision results

| Interval | Floor clipping | Wall clipping | Failed complete capsule checks | Hanging |
|---|---:|---:|---:|---|
| 1 | 0 frames | 0 frames | 0 / 864 | Downward |
| 4 | 0 frames | 0 frames | 0 / 864 | Downward |
| 8 | 0 frames | 0 frames | 0 / 864 | Downward |

Both simulation paths and the subdivided visible paths were checked every frame against the test floor/wall bounds, using the cable's visible radius. None clipped. Complete simulation-span capsule diagnostics ran every fifth frame and also passed. The minimum physical-path clearance above the floor was 0.5 cm; the minimum rounded visible-path clearance was about 0.01 cm. All-point coverage remained correct even though the stored budgeted-mode point setting was 1.

The hanging loop's mounts were at Z=500 cm, with its lowest point reaching about Z=256.21 cm. Moving attachments were blocked at the wall instead of carrying the cable through it; their cable endpoints could lag the requested attachment transform.

## Method

- Full Collision Queries enabled; intervals tested in balanced order **1, 4, 8, 8, 1, 4, 4, 8, 1**.
- Three repetitions per interval, 360 game frames each, verified 1/60-second game timesteps. This gives 4,320 cable-frame geometry checks per interval across the four cases.
- The first 30 frames of each trial were excluded from CPU statistics, giving 990 timing samples per case and interval. Collision checks include those first 30 frames.
- Each cable had 32 segments / 33 simulation points, 8 solver iterations, 1/120-second substeps, 10 cm width, 0.5 cm collision skin, smooth penetration recovery enabled, and four visual subdivisions / 129 render samples. Self-collision, network synchronization, grabbing and movement restrictions were disabled for the controlled collision comparison.
- An unattached vertical cable was accelerated into a 10 cm thick wall at 1,000 cm/s² from game frame 60 to 180. A second cable's attachment actor moved 5 cm per frame through the same wall over that interval. The wall was 600 cm tall. The floor's top surface was Z=0.
- The hanging loop used a 400 cm mount separation and 650 cm cable length. The near-floor loop used the same mount separation, 550 cm length and 65 cm mounting height.
- The stable isolated project snapshot was used to avoid interfering with the open editor's modules. Its 17 cable source files match the working sources validated by the preceding build.

For this tested setup, **interval 8** is the best measured cost/accuracy choice: about 61% less median active cable time than interval 1, with no detected clipping. Interval 4 also passed and retains more intermediate solver checks. These tests cover this level and these motions; they do not establish a universal guarantee for arbitrary geometry, teleporting colliders, initially embedded anchors, or incompatible cable constraints. No default or placed actor setting was changed by this test.

Runner: `Scripts/TestCableFullCollisionIntervals.py`. Raw results: `Saved/CableFullIntervalValidation/Result.json`; concise comparison: `Comparison.json`; engine log: `Editor.log`. The runner refuses to execute without the isolated helper flag, NullRHI and a save directory inside the validation output folder.
