# Bottle lifecycle repair 14 November 2026

Current installation: **editor module 14120 / Editor 78 / Game 78** in ColossalFizzCo. The new Windows archive is Saved/Builds/NormalColaPrototype_14120/Windows/ColossalFizzCo.exe. Previous14119 remains available.

## Findings and fixes

| Issue | Measured cause | Correction |
| --- | --- | --- |
| Floating caps | Saved cap edges retained +170 cm despite identity mounts. Catalog migration ran before edges were restored. Existing server components could retain old poses. | Refresh the catalog after deserialization; repair mounted cap location/scale from the recipe, retain manual twist, update existing authoritative components and invalidate the save cache. Remove the hardcoded170 cm fallback. |
| Fallen or partial bottles at startup | The save already contains fallen poses and partial/empty stock. Filled uncapped resting bottles were promoted to physics regardless of the product policy. | Preserve saved history and poses; respect the product's resting-ISM policy during load and demotion. Moving bottles and incomplete attached caps retain active simulation. |
| Swelling without launch | Kinematic holders without parent attachment were classified as pickup transit indefinitely, suppressing motion-pressure sampling. Sampled fallen bottles also lacked caps/seals. | Bound transit suppression by warmup/grace. Subsequent held motion drives agitation. Suppress trapped-pressure morph on open bottles; respect the manufacturing cap hold guard in the final threshold check. |

Bottle, liquid and cap identities remain intact. No schema or replication fields were added. Existing bounded snapshots and the continuous pressure channel are reused.

## Saved state evidence

The final editor autosave was written at **08:04:56 UTC**, before editor shutdown at08:07UTC. It contains **9,266 instances,17 definitions and3,086 bottles**. Of those bottles, **571 are partial,85 empty and126 open**; categories overlap. Ten filled open bottles are saved at rest. There are **2,426 obsolete attached-cap offsets**.

An earlier07:38UTC snapshot was initially audited; the final editor save was then independently validated. Both are retained. Final save SHA-256: df4050e94a62ccaacd8183bf972fecd19eb40afd0796ee48bc230d350f969bbb.

Both restores retained every identity and root pose. Content, pressure, bottle/liquid creation timestamps, strength and cleanliness changed in **zero records**. Incorrect cap mounts after restore: **zero**. Tests never reset or overwrite player saves.

Previously saved partial/fallen bottles continue to load that way. This patch does not refill, heal, recap or reposition damaged stock. A fresh-stock reset requires a separate decision and backup.

## Verification

- Isolated Editor78: **66/66 native tests pass**,60 clean and6 warning-bearing.
- Installed14120 with live assets and final editor save: **66/66 pass**,60 clean and6 warning-bearing.
- New BottleLoadLifecycle regression: two save/load cases, retained manual twist/history, six ASM/ISM cycles, physical held motion, swelling, cap ejection, gradual depletion and physics handoff.
- Real editor-game peers: **14/14 checks pass** at75 ms packet lag and2% loss. The manufactured bottle was saved in a fallen pose with a stale cap edge, reloaded, picked up, shaken through motion samples, launched, drained and settled. The remote client observed the empty resting result.
- Packaged14120 three-sale run: **13/14 checks pass**. All gameplay/state checks and three sales completed. Its process-exit check remains unconfirmed because the old wall-clock budget expired and exit codes were unavailable afterward. Both peers wrote normal shutdown logs. The runner budget is corrected.
- Packaged14120 restored-bottle lifecycle with the corrected runner: **14/14 checks pass**, including natural process exits.
- Packaged14120 late join: **13/13 checks pass** with75 ms packet lag and2% loss.
- Rendered remote shelf evidence shows caps at bottle openings. Hardware input acceptance remains separate.

An intermediate partial-cap demotion regression was caught by the full suite and fixed before deployment. Incomplete attached caps retain active simulation. The final suites pass.

## Changed files

All below Source paths start at Source/ColossalFizzCo/Private.

- Props/ColaContainment.cpp
- Props/ColaPressure.cpp
- Props/ColaPropActor.cpp
- Props/InteractablePropManager.cpp
- Props/PropAssembly.cpp
- Tests/BottleLoadLifecycleTests.cpp
- Tests/NormalColaPrototypePressure.cpp
- Tests/NormalColaPrototypeTestActor.cpp
- Scripts/TestColaAssemblyLine.ps1 at project root

The runner now allows a bounded wall-clock margin for its game-time exit ticker. No gameplay binary change was needed for that harness correction. No Blueprint, map, mesh, material or scale changes were needed.

## Remaining acceptance

1. Reopen ColossalFizzCo to load14120. Test real G/GG pickup/release on host and remote client. Shake/drop a full sealed bottle through launch, draining, physics and settlement.
2. Inspect existing saved shelves. Partial/fallen stock remains; mounted caps should align. Uncapped bottles need proper capping before a sealed-pressure launch.
3. Six native tests pass with warnings. Existing animation/collision/material warnings and missing legacy classes remain documented separately.
4. Earlier probing observed some out-of-world records below the map. Recovery/retirement requires separate investigation; this patch preserves their saved history.
5. Long save/load soak, large-stock network/performance measurement and designer pressure tuning remain separate.
6. The DOCX checkpoint is saved and passes ZIP/XML checks. Pagination could not be visually verified because LibreOffice soffice.exe is absent.

## Evidence and resume

Live deployment: Saved/BottleLifecycle_20261114/Deployment14120.json. Backups include previous source, module metadata and both autosave snapshots. The prior14119 DLL remains. Installed14120 is byte-identical to tested Editor78; source trees match.

Complete isolated evidence is under C:/Users/PC/Documents/Unreal Projects/ColossalFizzCo/Saved/ModularWork:

- Saved/BottleLifecycleBug_20261114/Checkpoint.json
- Saved/BottleLifecycleBug_20261114/RuntimeCapEvidence.json
- Saved/BottleLifecycleBug_20261114/SourceManifest.json
- Saved/BottleLifecycleBug_20261114/BottleLifecycle.patch
- Saved/Automation/BottleLifecycleFull78/index.json
- Saved/Automation/BottleLifecycleLive14120/index.json
- Saved/ManufacturingQA/Network_20261114_135954_17934/Result.json
- Saved/ManufacturingQA/Network_20261114_140710_17935/Result.json
- Saved/ManufacturingQA/Network_20261114_140913_17936/Result.json
- Saved/ManufacturingQA/Network_20261114_141752_17937/Result.json

Native filter: ColossalFizzCo or focused ColossalFizzCo.Props.BottleLoadLifecycle. Optional -BottleSourceSave=<copy> audits a save read-only. Repeat restored pressure through Scripts/TestColaAssemblyLine.ps1 -ManufacturingOnly -ReloadBottlePressureTest -PacketLagMs 75 -PacketLossPercent 2 and an unused -Port. Add -GameExecutable pointing to the packaged child executable for packaged checks. Run -LateJoin separately without pressure.
