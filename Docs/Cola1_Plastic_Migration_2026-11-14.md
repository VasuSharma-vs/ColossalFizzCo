# Cola1 replacement with the supplied plastic bottle — 14 November 2026

Status: installed and validated in ColossalFizzCo. Native module 14119 is the exact Editor70 binary that passed the isolated suite; the live source tree matches that compiled tree. Game70 is archived as NormalColaPrototype_14119.

## Completed work
- Updated all 13 legacy/prototype bottle product assets to SM_PlasticBottle and SKM_PlasticBottle. Plastic profiles replace inherited glass fracture links. Existing source mesh and material files were preserved.
- Kept shell material slot 0 separate from liquid presentation through the shared LiquidMaterial asset field and existing managed liquid child. Reused the existing modular assembly, normal cap and content catalog.
- Updated working Blueprint component defaults, maps and World Partition actors. Actor scales were not normalized or deliberately repositioned.
- Replaced the Cola1 geometry with a 1,296-byte compatibility redirector. Cooked builds register the old package/object paths. The prop-save reader explicitly resolves object-name redirects while retaining the engine save header, schema and string encoding.
- Refresh old saved bottle presentation/physical profiles from migrated source assets while preserving PID/ISID, price, contents, fill, damage, cleanliness, timestamps, pressure and world transform. Preserve custom tuning once a definition is current.
- Initialize a real normal-cap identity for a migrated sealed bottle that formerly had a virtual seal. Preserve existing caps and leave opened bottles open.
- Updated the GDD size-reference row to the supplied plastic shell at native actor scale 1.

## Validation
- Isolated Editor70 and live module14119: 65/65 native tests pass (59 clean, 6 warning-bearing).
- New regression serializes a legacy named-object reference and loads it through the actual save reader; another restores a legacy bottle and checks persistent identity/history, rendering and physical cap identity.
- Standalone Game70 builds; final official UAT stage/archive succeeds. Archived executable hash matches Game70. Default launch opens Lvl_NormalColaPrototype and exits cleanly.
- Full packaged host/rendered-client scenario: 13/13 checks pass under configured 75 ms lag and 2% loss. Three customer sales, bucket return/departure, cash deposit, supply purchase, manual manufacturing/closure, stocking and pressure-flight/handoff pass. Peers agree: three receipts, bank 9550 cents, register 0; cylinder 0.900. Pressure ends empty and settled.
- Separate archived-package late join: 13/13 checks pass, including existing product state, matching bottle/cap/liquid identities and two initial snapshot acknowledgments.
- Actual cooked legacy-save reader passes on host, client and late client. Earlier failed raw-load/save-reader preflights are historical diagnosis, not the accepted build.
- Reviewed remote-client captures: partial ASM liquid and capped stocked ISMs use the supplied bottle; no nested old bottle appears in those views. Full animation/orientation acceptance remains manual.
- Thirteen product prices, portions, default contents, expiry/taste/stimulus values were checked unchanged during migration. All 98 existing save files retain their pre-migration hashes. Supplied plastic source art is unchanged.

## Known remaining issue and designer decision
101 pre-existing external actor packages in Lvl_FirstPerson cannot load because their Blueprint classes are missing/broken: 81 reference BP_FireCola and 20 reference BP_SweetCola. They retain old mesh paths, which resolve through the compatibility redirector, but their missing gameplay classes are not repaired by a mesh migration.

Decision needed: restore the original missing classes from a known backup, or approve migration to specifically selected current classes. No substitute behavior was invented. These broken actors are outside the working Normal Cola prototype validation.

Legacy skeletal preview/physics resources were retained; working bottle assets/Blueprints use the supplied new skeletal mesh.

## Remaining acceptance
Actual G/GG/hold-G and E/F/R hardware input, designer handling/pressure balance, all morph animations/orientations, a long repeated-visit/save/load soak and measured CPU/GPU/network costs remain separate acceptance. Six native tests have warnings; experimental Python toolset startup warnings and missing original-map classes remain documented. No measured bandwidth or full production-readiness claim is made.

## Evidence and recovery
Live: Saved/Cola1Migration (asset/source backups, manifests, native install, logs and final audit).
Live native: Saved/Automation/Cola1MigrationValidation_14119_20261114/index.json.
Packaged candidate: Saved/Builds/NormalColaPrototype_14119/Windows/ColossalFizzCo.exe.
Evidence: Saved/Cola1MigrationEvidence_20261114.
Recovery archive: Saved/Checkpoints/Cola1Migration_14119_20261114.zip.
The isolated workspace remains under Saved/ModularWork. Do not rerun one-time source installers. Reopen ColossalFizzCo normally to use module14119.

## Modified source/script/GDD paths
- Docs/GDD_Master.md
- Scripts/AuditLegacyCola1.py
- Scripts/ConfigureBottleFragmentAssets.py
- Scripts/MigrateLegacyCola1.py
- Scripts/RunCola1Migration.ps1
- Scripts/TestColaAssemblyLine.ps1
- Source/ColossalFizzCo/ColossalFizzCo.cpp
- Source/ColossalFizzCo/Private/Props/InteractablePropDatabase.cpp
- Source/ColossalFizzCo/Private/Props/InteractablePropManager.cpp
- Source/ColossalFizzCo/Private/Props/PropAssembly.cpp
- Source/ColossalFizzCo/Private/Props/PropCapAssembly.cpp
- Source/ColossalFizzCo/Private/Shop/ShopProcurementComponent.cpp
- Source/ColossalFizzCo/Private/Tests/ColaLiquidIntegrationTests.cpp
- Source/ColossalFizzCo/Private/Tests/FragmentLifecycleTests.cpp
- Source/ColossalFizzCo/Private/Tests/LegacyPlasticBottleMigrationTests.cpp
- Source/ColossalFizzCo/Private/Tests/NormalColaPrototypeTestActor.cpp
- Source/ColossalFizzCo/Public/Props/InteractablePropDataAsset.h
- Source/ColossalFizzCo/Public/Props/InteractablePropDatabase.h
- Source/ColossalFizzCo/Public/Shop/ShopProcurementComponent.h
