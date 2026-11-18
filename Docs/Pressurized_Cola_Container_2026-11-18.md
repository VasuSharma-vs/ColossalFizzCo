# Pressurized cola container and receiving sockets

Implemented 18 November 2026. Editor module 18103 builds; all 70 selected runtime regressions pass, including seven new pressurized-supply cases.

## Assets and player controls

`/Game/Assest/Blueprints/Cola_Container/BP_ColaCointainer` uses the native `APressurizedColaContainer`. Its existing `ContentTankers` mesh, scale and material overrides are preserved. The actor adds an outlet, an animated valve wheel and a readout for remaining cola, delivered total, current outlet flow, pressure and temperature.

`BP_ColaHose` in the same folder is a reusable `AHoseCableActor` preset with two connector meshes. Existing content hoses also work; the same transport component can be used by pipe actors.

1. Place the container, hose and manual filling machine. Grab a hose end with **E**, aim at a compatible connection and press **E** again. The start end attaches to the source; the other end attaches to the machine's selected input sphere.
2. Aim at the valve wheel. **E** turns it toward open; **Shift+E** turns it toward closed. Native defaults use 90-degree steps over 720 degrees of total valve travel. The valve starts closed.
3. Seat a compatible bottle, choose the connected machine input and hold the filling lever. Demand starts only after the cradle reaches its filling pose. The container must also have liquid and an open valve.
4. Releasing/stopping the filling lever closes demand. The hose remains connected and retains its contents. Explicit disconnection, socket overload or hose rupture detaches the relevant connector.

The source outlet accepts one hose at a time, including a hose whose far end has not yet been connected. Machine sockets are also single-hose connections.

## Editable limits

Pressure values below are shared **gameplay pressure units**, not PSI or a thermodynamic calculation. Temperature is stored and mixed in degrees Celsius; opening the valve does not invent or delete heat during liquid transfer.

| Owner | Settings / starting preset |
| --- | --- |
| Container `SupplyPort` | Content `ResourceID` from the product catalog; capacity 200 portions; one connected hose. |
| Container actor | `InitialUnits = 200`, `OutletTemperature = 85`, full-open `OutletPressure = 8`, `MaxOutletUnitsPerSecond = 8`. |
| Container valve | `ValveOpening` 0–1 scales offered pressure and source rate. Zero closes supply without breaking the connection. |
| Hose transfer | Normal rate 5 portions/s, finite buffer 2 portions, burst pressure 4, overload delay 0.25 s. Pump/pressure boosts remain available, bounded by the hose's driven-rate limit. |
| Machine sockets | Maximum incoming pressure 6; maximum actual offered intake 10 portions/s. `HosePortContentIDs` accepts only the configured positive ID; zero lets an unoccupied input adopt its connected content type. |
| Bottle | `ManufacturingMaxSupplyPressure = 3`; bottle-owned failure dispatch decides the reaction. Normal plastic cola uses its existing rocket release. |

These defaults intentionally make a fully open tank unsafe. At a quarter-open valve, the outlet offers pressure 2 and at most 2 portions/s. Change ratings and the bottle limit to tune the intended operating range. The hose's own buffer and source availability may reduce delivered flow further.

The source limit caps **intake into the hose**. Previously stored hose liquid can drain faster if the receiver requests it and the hose permits it. Closing the source valve allows that trapped liquid to drain; closing receiver demand retains it.

## Transfer and failure ownership

```text
Container amount + valve + temperature + source pressure/rate
  -> hose request -> finite hose inventory -> rated delivery
  -> typed machine socket -> seated bottle's canonical inventory
```

The machine drives the selected hose once per fixed filling step. It does not also open a direct `BeginLiquidTransfer` session. Received units determine actual fill level and the displayed portions/second. A positive target fill level limits accepted volume before delivery, so a fast hose cannot overshoot it.

`UResourceTransferEndpointComponent` separates connector capability (`bCanSupply` / `bCanReceive`) from operational state (`bSupplyEnabled` / `bRequestsFlow`). Closed valves and idle machine sockets can still be connected. `IsSupplyRequested()` exposes the source request state. `TotalSuppliedUnits` counts successfully debited units for the container's meter, including liquid remaining inside a hose.

Overload checks happen before the offending transfer debit. A socket exceeding its configured incoming pressure or offered-flow limit pops its connection, stops the selected request and keeps trapped liquid in the hose. The socket does not silently throttle excess flow. A hose that reaches its own pressure/time limit bursts, releases its remaining inventory once and disconnects both ends. Whichever failure occurs first stops subsequent transfers; socket and hose thresholds are independently editable.

After accepted delivery, `OnResourceReceived(Socket,Hose,ActualUnits,IncomingPressure,ActualUnitsPerSecond)` informs the bottle. Excessive incoming pressure dispatches its existing manufacturing failure immediately, including while the lever is held. The station releases the bottle restraint and stops requesting liquid. The source, hose and station never spawn a replacement rocket themselves.

The receiving endpoint can be parked without a bottle. It rebinds to the next seated bottle only with demand off. This preserves the physical hose connection and trapped contents between bottles. Fuel and electricity remain separate categories; content IDs cannot be relabeled while retained liquid is present.

## Inventory, temperature and authority

The new tank has a local finite inventory. Its explicit `bAllowManagedContentBridge` option permits Content delivery into managed bottles. Ordinary mixed local/managed endpoints still reject this connection unless the local endpoint opts in. Managed-to-managed transfers retain their original provenance and manager checks.

Local content intake carries weighted temperature and the oldest creation time into the hose. Managed delivery credits the bottle through `APropManager::DeliverHoseContent`, preserving normal compatibility, capacity, cleanliness, age and manufacturing checks. Bottle level, material feedback and temperature update from actual accepted units.

For liquid originating in a managed prop, rupture/purge also records the release in `ReleasedLiquids`. For this local tank, release is counted by `TotalReleasedUnits` and `OnContentsReleased`; it does not invent a managed ISID or release record. The local tank is not registered as a purchasable or autosaved Prop Manager item by this update.

Supply changes, transfer, overload and bottle failure run on the server. Player valve RPCs check ownership, reach, visibility and request ordering. Replicated settings, meter values, endpoint attachments and filling telemetry support clients; real remote-client acceptance remains a separate validation step.

Local inventory transfers use an amount that both float stores can represent, rounded down within the requested rate and capacity, so a nearly closed valve cannot create liquid through rounding. Destroying a hose from its delivery callback releases the remaining buffer exactly once, including the managed spill ledger when applicable. The separate [machine client-visual repair](Manual_Filler_Client_Visuals_2026-11-18.md) has passed real host/client/late-join verification; the complete hot-supply interaction has native regression coverage only.

## Files and verification

- Container: `Public/Resources/PressurizedColaContainer.h`, `Private/Resources/PressurizedColaContainer.cpp`.
- Player control: `Private/Shop/ShopColaContainerInteraction.cpp`, existing `ShopSupplyHoseInteraction.cpp`.
- Transport: `ResourceTransferEndpointComponent`, `HoseTransferComponent`, `HoseCableActor`.
- Machine/bottle: `ManualColaHoseFilling.cpp`, `BottleFillingComponent`, `ShopManualFilling.cpp`, `ColaManufacturing.cpp`.
- Asset setup: `Scripts/ConfigureColaContainer.py`; original requested assets are backed up in `Saved/ColaContainer_20261118/Before/`.
- Regressions: `Private/Tests/PressurizedColaSupplyTests.cpp`, existing hose/manufacturing/bottle suites.

Validation: Editor Development module **18103** builds. The saved tank, hose, manual machine and legacy supply Blueprints compile. The selected Props, Hose, ManualSoloFilling and Manufacturing and manual-machine collision suites pass **70/70** (51 clean, 19 with warnings; zero failures or skipped cases). Coverage includes finite source/hose/bottle conservation, hot-content delivery, closed-valve behavior, actual delivered fill rate, target-level clamping, pressure/rate socket overload, hose rupture, bottle rocket failure and single-outlet exclusivity, representable local inventory transfers, release accounting when a delivery callback destroys its hose, and safe client pool reuse. The saved-preset test spawns the actual tank Blueprint in a Game world and verifies the original `ContentTankers` mesh at scale 5; the Python commandlet's editor preview component list is not used as runtime mesh evidence.

Final build and test evidence: `Saved/ColaContainer_20261118/Final_18103/Build.log`, `Tests.log` and `Report/index.json`. Saved asset evidence: `Saved/ColaContainer_20261118/Assets.log` and `AssetConfiguration.json`. Warning-bearing tests retain lifecycle diagnostics, fixture collision/simulation warnings and the existing `ABP_Unarmed` animation warning. Restart UE5 to load the native class changes; all network peers need the same binaries. Rendered player interaction, remote multiplayer, packaging and persistence of this new local tank have not been accepted.
