# Cola manufacturing assembly line — 13 November 2026

Implemented in the confirmed `ColossalFizzCo` project. The originally attached `MarryByMidnight` directory was empty. Scope remains existing plastic bottles, Normal manual twist caps, Normal bulk cola, the existing workbench/Manual Wheel Rotator, display shelves, NPC shopping and checkout. No additional machine or bottle type was introduced.

## Player-facing result

Both `/Game/NormalColaPrototype/Lvl_NormalColaPrototype` and its `_Validation` twin start with ten empty bottles, ten Normal caps and one full existing Lemonade Normal Bulk Cola Container on a supported rack in front of the outboard workbench. The existing 356 starter sale bottles remain unchanged. The map migration grants no runtime inventory, does not debit the bank, and does not replenish materials on save reload.

Use the current Editor build and [play guide](Normal_Cola_Prototype_Play_Guide.md). Earlier packaged archives do not contain these changes; no new cooked package was produced by this task.

Hold G to collect/place a bottle, bring the source and cap to their marked workbench inputs, press E and choose Fill from the bulk container, attach the cap, cooperate with G holding/R rotation, then carry the sealed bottle to a display shelf. A nominal four-portion bottle consumes 10% of a forty-portion source. Existing two-player manual-cap roles and controls are preserved.

## Integration changes

- Filling can reconnect to an unfinished open bottle containing the same cola. Existing damage, cleanliness, content identity, physical capacity, reservation and source expiry rules remain enforced. Resuming retains the bottle's liquid creation time and conserves actual portions. Transfer sessions cannot form chains or compete for one target.
- The first accepted liquid sets the bottle to its source temperature. Further accepted portions mix temperature by actual liquid quantity, then publish through the existing continuous-state channel. Content changes still drive the existing mass, material, pressure and morph systems.
- Each partial wheel turn commits `CapSealed` below 1 through the existing per-bottle event channel. Returning operators recover their progress from that canonical state. Full closure still commits through `ClosePhysicalCap`. Station teardown clears its transient holding reference.
- Open filled bottles retain active liquid/pressure simulation and reject pool return. This preserves unfinished contents/progress instead of freezing the simulation. Finished assemblies keep their managed bottle/cap identities, temperature, pressure, content and product metadata across existing ASM/ISM and save/load paths.
- Existing shelf registration, dynamic NPC stock selection and checkout are reused. Tests explicitly reject uncapped shelf stock and recheck products opened after registration.
- Failed filler connection attempts clear provisional endpoint identities; invalid connection-distance settings are rejected.

## Validation results

| Check | Result | Evidence |
| --- | --- | --- |
| Baseline native suite | 57 clean passes, 4 passes with warnings, 1 failure | `Saved/Automation/Manufacturing_Baseline_20261113/index.json` |
| Final Editor build | Succeeded | `Saved/Logs/Manufacturing_Build_Verified_20261113.log` |
| Development game build | Succeeded | `Saved/Logs/Manufacturing_GameBuild_20261113.log` |
| Final native suite | 62/62 passed: 58 clean, 4 with warnings; zero failed/unrun | `Saved/Automation/Manufacturing_Verified_20261113/index.json` |
| Starter stock migration | Both maps: 21 raw-material identities, supported separated native-scale footprints; rerun succeeded without duplicates | `Saved/ManufacturingQA/Assets_20261113/AssemblyLineAssets.json` |
| Real server/client plus late join | All 8 checks passed with verified 75 ms outgoing lag and 2% packet loss | `Saved/ManufacturingQA/Network_20261113_102847/Result.json` |
| Rendered smoke check | D3D12 playable map opened; raw stock visible; instruction sign orientation corrected; test session closed normally | `Saved/Logs/Manufacturing_Rendered_Visible_20261113.log` |

The native full-loop test now picks up/carries/places a real empty bottle before filling, rejects full-but-uncapped shelf stock, seals through two possessed player operators, restocks, checks shopping availability, and verifies disk persistence. Other exercised paths include interrupted/reconnected filling, retained liquid age, incompatible contents, source quantity conservation, weighted temperature, manual-role conflicts and spam limits, leaving/resuming cap rotation, full bottle/cap linkage, mass and velocity behavior, liquid material continuity, pressure/morph behavior, stale actions, checkout reservations and invalid interactions. The former liquid-representation failure used a stale hard-coded height; it now checks the configured material dimensions in both representations.

The network run completes two customer sales, banks the proceeds, purchases supplies through owned player RPCs, fills and manually seals a full Mint Normal Cola, carries and places it on a supported shelf, and registers it as customer stock. Server, remote client and late join agree on content 902, level 1.000, seal 1.000, physical cap ISID 10743 and source level 0.900; bank 4450 cents, physical shop cash 0, two receipts. Both original peers exit normally. This is a real networked instrumented service/input-component test, not a full hardware-driven player session.

## Remaining acceptance limits

The four successful tests with warnings report existing incompatible collision/simulation options in the Heavy/Lightweight BottlePawnPhysics, PlasticPressure and ManualWheelRotator fixtures. The rendered run also logs existing translucent-material/Nanite warnings for `Cola1` and a Python toolset startup error. These are retained rather than reported as fixed. A complete manual two-player playthrough, visual inspection of every liquid/cap/pressure effect, and a new cooked-package acceptance pass remain unverified. The task does not add hot-source cooling, hoses, mixing equipment or future production machinery.

## Reproduce and review

Run native tests with UnrealEditor-Cmd and `-ExecCmds="Automation RunTests ColossalFizzCo" -TestExit="Automation Test Queue Empty"`, an isolated `-UserDir`, and a report output directory. Reproduce the network check from the project folder with:

```powershell
./Scripts/TestColaAssemblyLine.ps1 -PacketLagMs 75 -PacketLossPercent 2 -LateJoin
```

`Scripts/ConfigureColaAssemblyLine.py` is the idempotent scoped editor migration, with map backups under `Saved/ManufacturingQA/Assets_20261113/Before`. Source backups are under `Saved/ManufacturingQA/Before_20261113`. Only the isolated prototype maps are saved by the migration. Normal player progress and original-world saves were not used for testing.
