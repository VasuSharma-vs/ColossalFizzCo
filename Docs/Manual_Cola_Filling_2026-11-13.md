# Manual Normal Cola filling update — 13 November 2026

The supplied manual-filling design is implemented in `ColossalFizzCo`. Both isolated prototype maps use the managed manual machine. Original-world maps and normal player saves were not used for testing. Existing plastic bottles, Normal Cola contents, Normal twist caps, liquid transfer, pressure/rocket flight, carry, shelf stock, checkout and team banking remain the underlying systems.

## Player workflow

See [the play guide](Normal_Cola_Prototype_Play_Guide.md) for controls. Connect one Normal bulk supply, place and align one bottle, then hold F or the lever button to fill. Releasing stops flow. Either apply the limited temporary seal and twist the physical cap with R before expiry, or leave the bottle open to vent before twisting. A second player is optional; players may prepare, fill, seal and carry independently across multiple purchased machines.

The initial stock remains ten empty plastic bottles, ten Normal caps and one full Normal bulk container. The starter machine is automatically registered with the Prop Manager. Additional machines cost a configurable $50 and use the existing bank, delivery and character carry paths. Each machine has one holder, one nozzle, a moving lever, a temporary-seal button and one visible source pipe. No power or future manufacturing machines were added.

## Editable initial settings

| Setting | Initial value |
| --- | --- |
| Fill rate | 1 portion/second; four portions per standard bottle |
| Maximum connection distance | 400 cm |
| Nominal full cutoff | Content level 1.0 |
| Added residual pressure | 0.60 per full content level actually transferred |
| Open manual-fill venting | 0.15 pressure/second |
| Safe residual pressure | 0.10 |
| Temporary seal | 3 seconds, once per fill |
| Lever refresh / lease | 0.10 / 0.40 seconds |
| Additional manual machine | 5,000 bank cents |

## Integration and interruptions

The server validates equipment range, alignment, possession, input session, source type, exclusive lever ownership and cap roles. A release request closes the transfer immediately; leaving range, disconnecting or missing the lever lease also stops it. Closing or losing focus on the terminal releases the lever. Moving a machine stops filling and releases its previous seated bottle to physics. Powered rocket flight retains its own physics control.

Actual accepted portions drive source depletion, bottle content, existing mass/material/liquid simulation and temperature mixing. Filling generates residual pressure; an open manual-filled bottle vents naturally. Temporary protection retains that pressure and suppresses manufacturing leakage. A proper cap cancels the deadline. Expiry with significant residual pressure and an unsecured cap invokes the existing cap-release and original/Chaos rocket implementations.

Temporary deadline, one-use state, manual-fill marker, partial cap closure and linked supply identity are canonical per-instance state. Event deltas, ASM/ISM binding, save/load and late snapshots carry those fields. The deadline uses the persistent manager clock, so loading does not restart the duration. Filled open bottles stay active for pressure/liquid simulation; working equipment remains active. Multiple machines can draw from one container with exact portion accounting and no imposed machine order.

A small queue correction projects a tilted queue marker onto the floor before spacing customers. The maps' existing checkout/player/queue placement is preserved.

## Validation

- Editor and non-editor Development game targets compile successfully. Logs: `Saved/Logs/ManualFilling_Build_9_20261113.log` and `Saved/Logs/ManualFilling_GameBuild_20261113.log`.
- Final native rerun: **64/64 passed**, 58 clean and six warning passes, zero failed/unrun: `Saved/Automation/ManualFilling_Final_2_20261113/index.json`. The new manual-loop test passes cleanly, including purchased-machine character RPC pickup/release and moved-holder interruption coverage. Four warnings are the existing physics fixtures; two tests additionally captured audio-device sample-rate conversion warnings during a device change. The previous pass before the final carry/interruption assertions was 60 clean plus four warning passes.
- Map/catalog configuration and read-only audit: `Saved/ManualFillingUpdate/AssetConfiguration.json` and `Saved/ManualFillingUpdate/SavedMapAudit.json`.

The manual-loop regression exercises starter registration, transactional purchase of multiple machines, actual character carry RPCs, real widget F/mouse callbacks, release/focus loss, lever ownership and timeout, partial-fill resume, temporary protection and repeat-use rejection, safe solo capping, simultaneous shared-source filling, disk persistence and pressure failure. Existing suites cover liquid/temperature/mass behavior, cap integrity, invalid interactions, stock registration, NPC product selection, checkout and bottle ASM/ISM restoration.

## Real network scope

`Scripts/TestColaAssemblyLine.ps1 -ManufacturingOnly -PacketLagMs 75 -PacketLossPercent 2 -LateJoin` runs a listen server, a remote player and a later joining client with separate saves. This explicit validation-map mode supplies isolated test funds and exercises owned purchase/action RPCs, manual held filling, natural venting, solo closure, carried shelf placement and matching product state. It does not count NPC customer sales as accepted.

Earlier broader runs on the current validation layout did not complete the whole route/checkout/manufacturing sequence within their bounded time; evidence is retained under `Saved/ManufacturingQA/Network_20261113_120229`, `_121440` and `_122818`. The first isolated run `_124628` also failed to reach manufacturing because its driver selected the customer path before the remote pawn arrived; that startup branch has been corrected. These failed runs are not manufacturing acceptance results. The earlier assembly-line record documents historical successful multiplayer evidence for the previous mandatory-cooperation implementation.

The corrected run `Saved/ManufacturingQA/Network_20261113_125254` reached manufacturing acceptance on the server, remote client and late join with verified 75 ms lag / 2% packet loss. Their snapshots agree: Normal Mint content 902, bottle level 1.000, seal 1.000, physical cap 10736, source level 0.900, bank 7550 cents, cash zero and zero sales receipts. Isolated validation funding is deliberate; this result excludes the NPC checkout route. **All eight final network checks passed**, including matching peer state, late join, verified emulation and graceful exits; see `Result.json`.

## Acceptance limits and reproduction

The four existing warning tests are Heavy/Lightweight BottlePawnPhysics, PlasticPressure and ManualWheelRotator fixtures, reporting incompatible collision/simulation settings. They are not claimed fixed. An isolated rendered playable-map preview was opened (`Saved/Logs/ManualFilling_Rendered_20261113.log`); Computer Use was stopped by the user with Escape before inspection, so it is not visual acceptance evidence. A full player-driven two-person hardware session, visual inspection of every liquid/cap/rocket effect and a newly cooked package remain separate acceptance work. Older packaged archives do not contain this update.

Run the native suite with UnrealEditor-Cmd, `-ExecCmds="Automation RunTests ColossalFizzCo"`, `-TestExit="Automation Test Queue Empty"`, `-nullrhi`, an isolated `-UserDir` and a report export directory. The scoped idempotent asset migration is `Scripts/ConfigureManualColaFilling.py`; its source/asset backups are under `Saved/ManualFillingUpdate/Before` and `BeforeAssets`. The supplied design is preserved verbatim in [Normal_Cola_Manual_Filling_Design.md](Normal_Cola_Manual_Filling_Design.md).
