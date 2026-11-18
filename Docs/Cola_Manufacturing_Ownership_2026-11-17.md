# Bottle-owned manufacturing and tilting filler

18 November extension: [pressurized container and hose delivery](Pressurized_Cola_Container_2026-11-18.md) replaces the manual station's direct transfer session with finite hose delivery. Actual accepted units govern fill speed; incoming pressure is evaluated by the bottle's `ManufacturingMaxSupplyPressure`. The historical build and validation below describe the original manufacturing change.

BP_Cola inherits manufacturing rules from `AColaPropActor`. Product/content definitions still provide the supported liquid and cap combinations, capacity, strength, torque range and material behavior. The bottle decides whether filling, sealing and capping are allowed and which failure to invoke. The prop manager commits inventory, transfers, cap links and persistent instance state. BP_ManualColaFillingMachine supplies player input, physical supply inventory and presentation.

## Filling motion

The reference images describe a long bottle cradle on a central pivot and a separate mechanical lever. The implementation provides editable prototype geometry for that mechanism; it does not recreate the illustrated brass artwork.

The bottle's local +Z axis points toward its cap. The cradle rests at 70 degrees pitch, with the neck slightly higher than the bottom. Holding the lever rotates it to 110 degrees, lowering the neck and raising the bottom. Travel takes 0.6 seconds by default. The holder, cradle platform, nozzle and clamps move together. Liquid transfer starts only after the bottle remains seated and the cradle reaches its filling position. Releasing the lever immediately stops transfer and returns the cradle. A grabbed or launched bottle is released from machine control.

`RestCradlePitch`, `FillingCradlePitch` and `CradleTravelSeconds` are machine layout settings. `CradleAlpha` replicates the mechanical position. Native components can be given production meshes in the Blueprint viewport.

After loading, the idle machine can recover its cradle angle from a nearby bottle whose saved position and orientation match the holder within the configured travel. It restores seating with liquid flow stopped, without moving an unrelated bottle or extending its seal deadline.

## Bottle settings and events

BP_Cola and its children expose `Cola > Manufacturing` settings:

| Setting | Default | Purpose |
|---|---:|---|
| Supports Temporary Seal | true | Allows a temporary manufacturing seal. |
| Temporary Seal Duration | 4 s | Lifetime after lever release. |
| Safe Residual Pressure | 0.1 | Maximum residual pressure for unprotected release. |
| Manufacturing Pressure Per Content Level | 0.6 | Pressure added by a full bottle-volume transfer. |
| Manufacturing Overfill Failure Level | 1.1 | Filled fraction that triggers the overfill policy. |
| Manufacturing Leakage Level Per Second | 0.05 | Liquid loss from an unsupported, partially fitted cap. |

`Add Manufacturing Seal` applies seal state directly on the bottle. The station uses `BuildManufacturingSealDelta` to consume a physical tray seal and apply the bottle state in one manager transaction. `CanAcceptManufacturingContent` validates content before transfer; `OnManufacturingLiquidReceived` accounts for accepted liquid and pressure afterward. The manager conserves source and target portions.

`Attach Manufacturing Cap`, `Advance Manufacturing Cap` and `Close Manufacturing Cap` enforce bottle compatibility and closure rules. `UpdateManufacturingCapLeakage` owns liquid loss while tightening. A live temporary seal permits the player to fit and twist a cap during its countdown. A fully closed cap ends temporary protection; it still must withstand the bottle's actual total pressure.

Override **Handle Manufacturing Pressure Failure** in a cola child Blueprint to choose that cola's response. The default plastic-bottle response launches the existing rocket component. Other materials use the existing containment-failure path. The machine does not select rocketing or directly apply failure effects.

## Temporary seal lifecycle

1. A seal can be installed before filling or while the lever holds the bottle. It remains armed at full integrity.
2. The first lever release starts its deadline on the manager's world clock. A stocked tray may supply a seal automatically on unsafe release.
3. Integrity falls from one to zero. Pulling the lever again does not reset or extend the deadline.
4. If pressure settles safely or the cap is secured, expiry does not cause an unsealed-pressure failure. A closed cap remains subject to the bottle's total-pressure tolerance.
5. If the seal expires while the bottle is unprotected and residual pressure is unsafe, the bottle invokes its failure event. Normal plastic cola rockets.

`bTemporarySealUsed`, `bTemporarySealArmed`, `TemporarySealUntil` and `bManualFillPressure` live in canonical per-instance event data and travel through the existing save/event-sync paths. The armed flag distinguishes an installed seal awaiting lever release from a running absolute deadline. Reapplying a spent seal is rejected. Physics pooling cannot erase active seal state.

## Player controls and presentation

Seat a bottle, choose a compatible connected supply and hold **F**. The cradle tilts before liquid flows. **X** stops liquid while the lever remains held for controlled venting; **V** resumes it when allowed. **Apply temporary seal** installs a loaded tray seal. Releasing **F** starts degradation. **R** fits a cap and advances tightening. Pressure, liquid level and seal integrity in the dashboard are read from the bottle; the station's former pressure settings are retained only for asset compatibility.

The station still validates player reach, inventory ownership, input leases and exclusive operation. Mutations remain server-authoritative. All multiplayer peers must use the rebuilt binary because the event serializer now includes the armed-seal flag.

## Verification

Editor Development build succeeded with module suffix `15401`. Both requested master Blueprints compiled and saved; the three cola children compiled and expose the new settings and failure override. A spawned instance of the saved machine verified `BottleHolder`, nozzle and clamps under `CradlePivot`, the new input bounds and the neck-up resting pose.

The focused automation suite passed **14/14**, with zero failures and zero skipped tests. Eleven tests retain warning output (rocket state logging and fixture physics/collision warnings); this is not a warning-free run. Coverage includes armed seal save/network state, release/regrab timing, cap protection, unsafe expiry across a frame hitch, same-actor bottle reuse, unsupported content, nonplastic failure stopping flow, cradle gating, physical supply/cap conservation, save/load seating with valve closed, and the previously pending player hit receiver. The player Blueprint receiver is now saved with automatic hit force disabled.

Evidence under `Saved/ColaManufacturing_20261117`: `Build.log`, `AssetConfiguration.json`, `Assets.log`, and `Report/index.json`. The Blueprint commandlet also reports the existing `ABP_Unarmed` thread-safety warning. Verification used headless editor sessions; rendered PIE and remote multiplayer acceptance were not run for this change.

Restart the normal UE editor to load the native class changes. Configure bottle policy in **BP_Cola > Class Defaults > Cola > Manufacturing** and machine motion in **BP_ManualColaFillingMachine > Class Defaults > Machine > Cradle**. All network peers must use the rebuilt modules.
