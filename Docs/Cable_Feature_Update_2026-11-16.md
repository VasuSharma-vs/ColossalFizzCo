# Cable feature update — 16 November 2026

The custom `CollisionCableComponent` used by `/Game/Assest/Blueprints/BP_Cable_CollisionSafe` now implements the requested features. Requests 4 and 7 share **Enable Grabbing and Pulling**. The other additions have independent switches. The original `/Game/Assest/Blueprints/BP_Cable` remains separate.

## Controls

Select the **Cable** component and expand **Cable → Features**. These are editable on the Blueprint template or an individual placed instance, and writable in Blueprint. For runtime changes, call **Set Cable Feature Enabled** on the authoritative server; it also wakes affected state and refreshes movement bindings.

| Switch | Default | Effect |
|---|---|---|
| Enable World Collision | On | Existing line-trace/sweep world collision and its rotating point budget. |
| Enable Self Collision | Off | Separation between non-neighboring spans, independently of world collision. |
| Enable Sleeping | On | Per-point sleep and full simulation-tick shutdown, with independent wake handling. |
| Enable Point Weight | On | Point mass affects impulses, distance constraints, grab acceleration/rate and physics attachment loads. |
| Enable Grabbing and Pulling | On | Middle-point grabbing, multiple participants, directional contributions and arclength falloff. Turning it off releases active point grabs. |
| Enable Movement Restrictions | On | Restricts the actual holder/driver, including upward/downward travel, when cable length or grab lag requires it. |
| Enable Visual Subdivision | On | Lengthwise render samples without additional simulation points or cross-section sides. |

Additional controls remain in their corresponding categories:

- **Self Collision:** thickness in cm (`0` uses cable width), neighboring-span exclusion, solver iterations. Fully sleeping span pairs skip geometric separation checks.
- **Sleeping:** sleep velocity (default 1 cm/s), sleep delay (1 s), wake velocity (5 cm/s), wake displacement (0.5 cm), constraint tolerance and wake-monitor interval (0.2 s).
- **Weight:** default point mass (1 kg), optional indexed mass overrides, explicit fixed point indices, reference grab mass and maximum physics attachment force. Missing/zero overrides use the default mass; zero does not create an anchor. Blueprint point indices are zero-based.
- **Grabbing:** default movement rate (3 cm/s), maximum total force per participant (100 N), influence radius along the cable (150 cm), falloff exponent (1) and reach limit (350 cm). Force is distributed over the affected points, so increasing resolution does not multiply available Newtons.
- **Movement Restrictions:** grab movement allowance (30 cm), length slack tolerance (2 cm) and start/end world-anchor settings. Movable holders/physics bodies are resolved automatically. Unattached endpoints do not create world anchors.
- **Cable Rendering:** visual subdivisions (2, range 1–8), smoothing strength and an optional visual deviation cap. The 17 November update removes the forced collision-skin cap and corrects the tube's exterior face winding. Simulation point count remains `Num Segments + 1`; `Num Sides` remains independent. See [rendering and collision quality](Cable_Rendering_Collision_Update_2026-11-17.md).
- **Networking:** authoritative snapshots (default 10 Hz), change threshold (0.1 cm) and client interpolation rate. Unchanged geometry is not continuously streamed.
- **Collision:** line traces/sweeps, points per frame, friction, skin, player filtering and gradual penetration recovery retain their existing controls.

## Interaction and movement

The existing owned `ShopPlayerInteractionComponent` handles point-grab requests, release and target updates. It selects the nearest simulation point along the viewed cable span, checks reach and blocking visibility on the server, and uses server-configured participant settings. The existing E-input path calls it when no terminal takes the interaction. A second interaction releases the grab; existing supply-hose connection behavior is retained.

By default, player interaction uses the cable's **Default Grab Settings**. To configure a stronger participant, disable **Use Cable Default Grab Settings** on its `ShopPlayerInteractionComponent` and edit its **Cable Grab Settings**. For example, 3 + 3 + 15 cm/s yields a 21 cm/s directional drive where the influences overlap, before resistance and constraints. Equal opposing vectors cancel. Direct drive falls off using actual distance along the cable, not straight-line distance through a folded rope.

Server-side gameplay/vehicles can also use **Begin Point Grab**, **Update Point Grab** and **End Point Grab**. A target component/socket can supply the moving target. Grabbing accelerates the existing simulation; it does not pin or teleport a point to the target. Points beyond the influence radius still respond to normal forces and constraints.

The holder adapter follows component attachment parents and the existing carried-prop `InteractionOwner` relationship. For characters, it uses `OnCharacterMovementUpdated` to constrain the real character root and remove blocked outward velocity. Other drivers use a post-physics callback. Slack can unfold; free sagging points are not treated as fixed pivots. Grab resistance follows the moving rope point, allowing a free cable to travel with its pullers.

The custom Blueprint upgrade disconnects the old ongoing-interaction gravity/braking loop and its five-second tick shutdown. It wakes the native cable instead. Existing endpoint interfaces remain, and their begin/grab paths respect the combined grab switch. A backup is kept in `Saved/CableFeatureValidation/BP_Cable_CollisionSafe.BeforeFeatures.uasset`.

## Sleep cost and wake sources

Sleeping points retain positions and cached contacts. They skip integration and their budgeted surface checks. Meaningful neighbor velocity or strain wakes them instead of leaving them as fixed anchors. Full sleep disables the component's simulation tick and preserves the cached GPU mesh.

Wake sources include grabs, impulses, endpoint/support transform events, support deletion, and arriving/moving blocking bodies. Cached planes follow a moved support instead of remaining at its old world position. A broad overlap sensor supplies events where available. Because ordinary meshes can disable overlap events, an independent world timer also performs one aggregate broad-phase proximity query at the configured interval while points sleep. Persistent stationary support and gravity blocked by a cached floor do not themselves wake the cable.

**Collision Query Count** reports simulation surface queries for the current component tick. **Wake Query Count** separately counts these low-frequency broad-phase wake checks. Full sleep therefore means zero simulation ticks/point traces, with wake monitoring still active; it does not mean literally zero engine work. Newly arriving objects with overlap events disabled may wait up to the wake-monitor interval to be discovered.

## Validation

The main Development Editor target compiled successfully. The final native suite passed **31/31 tests, zero failures** (`Saved/CableFeatureValidation/FinalVerifiedReport/index.json`). Validation uses a separate project/source snapshot under `Saved/CableFeatureValidation/IsolatedProject`, so concurrent work replacing the main project's module files cannot invalidate the test process. The cable sources are compared against the working project in `SourceProof.json`. The private snapshot only renames two unrelated dashboard color locals to permit unity compilation; the cable implementation and cable tests match the working source.

Verified scenarios include:

- Independent feature switches; disabling grabbing releases current contributors.
- Non-neighboring self-contact separation with world collision disabled.
- Settled points/full cable sleep, no repeated floor-point queries, retained contacts and unchanged sleeping geometry.
- Independent timer/overlap wakes, moving/deleted support, and colliders with overlap events disabled.
- Neighbor propagation without permanent sleeping anchors; staggered settling under variable timesteps without false wakes from blocked gravity.
- Impossible fixed-anchor length constraints prevent full shutdown; correcting their separation permits sleep again.
- Heavier points move less under equal impulses and pulls, including less actual participant progress; fixed points remain a separate state.
- Middle-point selection, arclength influence `0,1,2,3,2,1,0`, additive `3+3+15=21` drive and cancellation of opposing contributions.
- Existing owned interaction rejects grabs through walls, out of reach, or with grabbing disabled, and applies server-configured participant overrides.
- An attached nozzle owned by the cable actor but parented to a character resolves the real character; slack, horizontal limits, falling limits and blocked velocity removal affect that character's root.
- Free ends do not create a fixed world-space anchor.
- Twelve simulation points produce 45 render points with four subdivisions, without changing cross-section sides; visual deviation is bounded.
- Unchanged snapshots are suppressed and moving snapshots obey the configured rate.
- Existing floor, thin-wall, corner, triangle, smooth recovery, player filtering and shared per-frame query-budget regression tests.

The saved custom Blueprint compiled after replacing one legacy movement loop and gating three grab paths. Its seven flags were toggled independently, all three existing meshes still ignore Pawn, and the original Blueprint hash was unchanged. See `BlueprintUpgrade.json`; only the custom variant was saved.

The separate DX11 SIE render check completed with no errors. It measured a hanging curve from Z=320 cm down to about Z=145.49 cm, kept twelve simulation points/45 render samples, and put all twelve floor points to sleep with zero simulation queries. The floor path passed a complete collision-clear check, and its render revision stayed unchanged for more than 30 additional frames. No source asset was changed by the render check. See `Rendering/Result.json` and its recorded scene-capture PNG. The capture uses base color to verify geometry, not production lighting or GPU/FPS performance.

Live multi-client transport, latency/prediction behavior, and an actual driven vehicle have **not** been playtested. Multiple contributors, vehicle-rate configuration, authoritative request guards and snapshot scheduling were exercised in native tests, rather than a networked vehicle session. Physics attachment force coupling is implemented; a separate rigid-body load benchmark remains unverified.

## Limits

- One collision point per frame remains a performance/accuracy tradeoff. Earlier level tests showed missed floor/wall contacts at that budget. These additions do not turn line traces or sparse sweeps into a no-clipping guarantee. Use adequate simulation resolution and a higher sweep budget for important contacts.
- World geometry must provide query-enabled blocking collision. Embedded fixed anchors and incompatible constraints can remain unresolved; gradual recovery intentionally takes time.
- Self-collision uses discrete span separation, not continuous self-contact detection at arbitrary speeds. Dense knots and impossible fixed configurations can need more iterations/resolution.
- A fast body with overlap events disabled can cross the sleeping cable between proximity polls. Enable overlap events, shorten the monitor interval, or call `WakeCable`/`AddImpulseToPoint` from the gameplay impact handler when that case matters.
- Render smoothing uses cached contact planes; the optional full collision mode also validates the rounded path with shape queries. It cannot fix a collision already missed by the underlying simulation. See the 17 November update for its CPU controls.
- Physics attachment loads are an approximation coupled to existing rigid bodies, not a full Chaos rope joint network. Character restrictions constrain movement after its normal update; live network prediction under latency needs multiplayer playtesting.
- Participant/RPC guards and bounded snapshot publication can be tested locally. A live multi-client/vehicle network session must be distinguished from those tests.

Native reflected-property changes appear when the editor next loads the rebuilt C++ module. No automatic editor close/restart is part of this update.
