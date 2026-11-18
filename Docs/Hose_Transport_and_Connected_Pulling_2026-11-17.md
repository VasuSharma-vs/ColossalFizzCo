# Hose transport and connected pulling

Implementation: 17 November 2026. Native gameplay lives in C++; the Blueprint variants expose configuration and connector art.

18 November extension: [pressurized cola supply](Pressurized_Cola_Container_2026-11-18.md) adds source valves/rate limits, receiver demand, typed machine inlets, hot-content delivery, socket pop-off and explicit local-to-managed content adapters. The results at the end of this older report validate the 17 November implementation; current validation is recorded in the new guide.

## Assets and components

| Blueprint | Purpose |
| --- | --- |
| `/Game/Resources/Hoses/BP_HoseCable` | Reusable master with collision cable, finite transfer buffer, and two connector meshes |
| `/Game/Resources/Hoses/BP_ContentHose` | Content only |
| `/Game/Resources/Hoses/BP_FuelHose` | Fuel only |
| `/Game/Resources/Hoses/BP_ElectricityHose` | Electricity only |
| `/Game/Resources/Endpoints/BP_ResourceReservoir` | Master actor with local storage socket, optional pump, and cable pull response |
| `/Game/Resources/Endpoints/BP_ContentReservoir` | Example local content store |
| `/Game/Resources/Endpoints/BP_FuelReservoir` | Example fuel store |
| `/Game/Resources/Endpoints/BP_ElectricityReservoir` | Example electrical energy store |

Each hose has independently replaceable `StartEndpointMesh` and `EndEndpointMesh`. Their offsets follow the simulated first and last cable points. The supplied cylinder meshes are placeholders; assign the connector artwork in a child Blueprint. Connector local Z points outward along its end tangent.

The responsibilities are separated:

- `UCollisionCableComponent`: cable particles, collision, rendering, grabbing, sleep and networking.
- `UHoseTransferComponent`: typed inventory, transfer rate, drive pressure, draining, purging and rupture.
- `UResourceTransferEndpointComponent`: a typed source/receiver socket and its local or managed store.
- `UResourcePumpComponent`: an optional motor's running state and push/pull strengths.
- `UCablePullResponseComponent`: an attached object's mass, movement resistance, pull force and fixed state.
- `UCableMovementConstraintComponent`: the runtime adapter that queries connected loads and applies farthest-first movement.
- `AHoseCableActor`: physical attachments, two connector meshes, rupture event and transfer connection lifecycle.

## Connecting and using a hose

1. Place a typed hose and two matching reservoirs. Reservoir examples start with 50 units in a 100-unit store; set the receiver's initial amount to zero when testing an empty tank.
2. Set the same `ResourceID` on the endpoints. Category is exclusive: an electricity hose cannot carry content or fuel. IDs distinguish fuels, content recipes or electrical systems within a category.
3. Connect the start to the source and the end to the receiver. In Blueprint call `ConnectStart` / `ConnectEnd` on the server, or assign `InitialStartPort` / `InitialEndPort` on a placed hose. The existing player cable interaction also supports grabbing a free end, aiming at a compatible actor, and pressing E to connect.
4. An empty hose may adopt the source's resource ID when `bMatchSourceResourceWhenEmpty` is enabled. Connect the source first when its ID differs from the hose default. Retained contents cannot be relabeled.
5. Call `DisconnectStart` / `DisconnectEnd` to unplug. This stops transport and retains the trapped amount. These Blueprint functions are available for custom connector interaction controls; pinned ends are not free grab points.

Multiple sockets on an actor are allowed. The resolver uses a compatible socket with the required supply/receive direction, rather than assuming the first component matches. An authored socket contract is respected; the managed fallback cannot bypass it. The `ResourceEndpointProvider` interface can designate a preferred socket.

Multiple hoses may share a reservoir socket when `MaxConnectedHoses` is zero (unlimited) or its configured count permits them, allowing a junction with finite storage. The new cola tank and machine inlets use a single connection. Incoming and outgoing transfers remain limited by each hose and by the junction's available inventory. A physical cable-to-cable attachment alone does not create a fluid connection: use an endpoint store for resource flow through a junction.

## Capacity, motors and failure

| Setting | Default | Meaning |
| --- | ---: | --- |
| `MaxUnitsPerSecond` | 5 | Baseline transfer rate |
| `BufferCapacityUnits` | 5 | Maximum amount physically held inside the hose |
| `RatedPressure` | 1 | Drive level above which flow is boosted |
| `FlowBoostPerPressure` | 1 | Rate multiplier increase per excess pressure unit |
| `MaxDrivenUnitsPerSecond` | 50 | Hard upper limit, including motor boosts |
| `BurstPressureThreshold` | 4 | Overpressure limit |
| `BurstDelay` | 0.5 seconds | Allowed time above the pressure limit |
| `BlockedPressureRisePerSecond` | 1 | Backpressure scale when a source pushes into a full, blocked hose |

Content units are portions. Fuel and electricity use the units authored by their storage systems. Pressure is an abstract gameplay value, not a calibrated fluid simulation.

Source push and receiver suction are added to obtain drive pressure. Socket `PushPressure` / `PullPressure` work directly. A socket can also reference a `ConnectedMotor`, or use its own `bMotorRunning` and `MotorPressureBoost`. A stopped motor adds no motor strength. `bRequiresMotorDrive` can prevent passive transfer when neither end has a running motor.

The requested rate is:

`min(MaxDrivenUnitsPerSecond, MaxUnitsPerSecond * (1 + max(0, DrivePressure - RatedPressure) * FlowBoostPerPressure))`

Actual intake is limited by source availability and hose space. Actual output is limited by hose contents and receiver capacity/admission. Source, buffer, receiver and released contents are accounted separately. A small buffer does not accidentally turn the per-second rate into a per-frame capacity limit: bounded substeps integrate the flow. Defaults are a 0.05-second maximum step and 128 steps per call. `DroppedTransferTime` reports unusually large intervals discarded when that work budget is exhausted.

A full blocked hose accumulates extra pressure while the source keeps pushing. Overload stops at the burst deadline within the current update. Rupture stops transfer, releases the stored payload once and detaches both connectors. Bind `OnHoseRuptured` for the explosion effect, sound and any desired damage. No explosion VFX or damage values are authored by the transport component itself. Electrical release represents dissipated energy, not spilled liquid.

To empty the hose into a receiver, keep both endpoints connected, turn **Intake Enabled** off and leave **Drain Enabled** on. `PurgeContents` releases or dissipates the trapped amount immediately, including when disconnected. Repeated purge calls on an empty hose release zero. `RepairHose` requires an empty buffer; reconnect the endpoints afterward.

## Integration with Cola and Prop Manager

`BP_ColaSupplyContainer` exposes a managed content socket and now has a mesh at both ends of its existing supply hose. A standalone content hose can attach to its supply socket and to an active Cola actor. `UResourcePortLibrary` creates a managed socket on compatible active props as needed, using the existing ISID and ContentID.

Managed transport debits and credits the canonical Prop Manager inventory. It preserves weighted temperature and the oldest content creation time; content cannot gain a fresh expiry timestamp by passing through a hose. Spilled managed contents enter `ReleasedLiquids` at the cable's current midpoint. Bottle compatibility, cap/seal admission, fill slowdown and manufacturing pressure/failure handling still belong to the bottle and its product definition.

Managed endpoints connect to managed endpoints belonging to the same Prop Manager. Local example reservoirs connect to other local stores by default. An explicit local Content endpoint may enable `bAllowManagedContentBridge` to feed canonical bottles, as the new pressurized tank does. Fuel/electricity and unapproved mixed endpoints remain separate. For an existing managed Cola supply, bind its endpoint with `ConfigureManagedContent`; do not assign a second `StoredUnits` inventory.

Prop pooling clears managed socket bindings and disconnects attached hoses before actor reuse. The hose retains its already-debited buffer, which can be reconnected to compatible endpoints or purged. Backend/manager/type constraints prevent relabeling retained liquid.

The manual machine now drives either the integrated `SupplyTransfer` or an attached standalone hose through the same finite-buffer transport. Its receiving socket binds directly to the seated bottle, and delivered units drive filling; no parallel direct filling session debits the source. Idle sockets park their bottle binding and close demand while keeping the physical connection and retained contents.

## Connected pulling

Slack contributes cable point weight without immediately dragging every attached object. Once available slack is exhausted, the graph query includes the next connected body's mass and resistance, then follows its other cables. Shared actors and cables are counted once, including branches and loops. Direct `CableStart` / `CableEnd` socket links are supported mechanically.

The solver checks the available pulling force and moves affected bodies from the farthest connection back toward the initiating actor. Sweeps stop movement against blocking world geometry, and the allowed displacement propagates back to the puller. Fixed endpoints and fixed interior points anchor the chain. This is a bounded gameplay movement solver, not a replacement for Chaos rigid-body joints. It translates bodies along the requested pull direction; it does not model pulley forces around moving contact bends.

On movable non-physics props add `CablePullResponseComponent`:

| Setting | Default | Meaning |
| --- | ---: | --- |
| `WeightKg` | 10 kg | Authored connected load |
| `ResistanceNewtons` | 0 N | Additional resistance before movement |
| `PullForceNewtons` | 1000 N | Force available when this actor drives a pull |
| `bFixed` | false | Prevent this actor being pulled |

Physics bodies without an override use their physical mass and 4.905 N/kg of drag resistance. Non-physics props without an explicit response remain fixed. Character pull force defaults to movement mass times maximum acceleration, converted from centimeters to meters; other drivers use the cable's default grab force unless they have an explicit response. Each cable's `PointMassKg` controls its own load. The default standalone hose uses 0.05 kg per point. Traversal is bounded to 128 connected actors/cables and a 4096-cable world registry; an incomplete query conservatively blocks movement instead of allowing stretch through an unexplored chain.

`QueryConnectedPull` reports load/required force without moving anything. `TryPullConnectedChain` moves remote bodies and returns `AllowedLocation` for the driver to apply. Runtime cable movement adapters use this automatically; client-side queries cannot move authoritative remote bodies.

After an intentional teleport, pool delivery or restored placement, call `ResetCable`. It reseeds the cable and resets the movement baseline so that restoration is not interpreted as a force-driven pull. Ordinary particle updates preserve movement history.

## Persistence and validation scope

Gameplay mutations and connection RPC validation run on the server; client components receive replicated state. Same-world checks, resource compatibility, reach, line of sight, ownership and monotonic interaction sequences protect player connections.

The hose buffer and local reservoir inventory have `SaveGame` fields, but generic hose/reservoir actors are not automatically included in Prop Manager's autosave. A world save system must capture and restore these actors and their connections to persist them across sessions. Do not claim save/load coverage from the flags alone.

Native automation covers conservation, type and authority rejection, motor boosts, blocked flow, rupture, purge, frame-rate independence, managed inventory, pool reuse, physical disconnects, owned connection validation and connected-chain movement. Rendered connector art, physical feel and real multi-client acceptance require a live gameplay pass.

### Verified results

- UE 5.8 Development Editor build, module suffix `15501`: passed after the final restoration fix.
- Native regression selection `ColossalFizzCo.Hose+ColossalFizzCo.Cable+ColossalFizzCo.Shop.ManualSoloFilling+ColossalFizzCo.Shop.LiquidTransfer`: **54/54 passed**, 52 clean and 2 warning-bearing. Includes 16 new hose/connected-pull tests and the existing cable/filling checks. Existing rocket debug/GetMass logging remains in the warning-bearing tests.
- Eight new resource Blueprints and the updated supply-container Blueprint compiled and saved. Existing affected Blueprints compile against the native implementation. The existing unarmed animation Blueprint emits its thread-safety warning during these checks.
- Fresh-process asset verification: **28/28 passed**, including all saved categories/capacities and two connector meshes separated by 400 cm on the spawned master.
- The first broader run exposed stale movement history during pooled supply restoration. Resetting that history on deliberate cable reset fixed the manual save/load filling regression; the final run includes a dedicated teleport/reset test.

Evidence: `Saved/HoseTransport_20261117/Build.log`, `Report/index.json`, `AssetConfiguration.json`, and `AssetVerification.json`. Initial failed-run evidence is retained as `InitialTestReport.json` and `InitialTests.log`. No packaged build, rendered playtest or real network session was run for this change.
