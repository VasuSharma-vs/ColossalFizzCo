# Manual filling machine client visuals

The reported client showed a red supply-cylinder mesh where the host showed the manual filling machine's cube pedestal. The cradle and lever remained present.

The saved machine Blueprint, its product definition and both prototype maps were inspected read-only. Their pedestal is `/Engine/BasicShapes/Cube.Cube` with `WorldGridMaterial`; the machine Blueprint has no stale bottle data-asset default. These assets do not need an artwork replacement.

Client prop-pool lookup could reuse an actor solely because its local ASM slot number matched the server's slot. It did not consistently require the correct actor class or an unbound/same-instance identity. Applying a cylinder definition to a machine actor replaces its `PropMesh` while leaving its machine components attached, matching the reported appearance.

`InteractablePropManager.cpp` now validates candidate class and instance ownership in client representation lookup and single/batched promotion handling. A cylinder cannot claim a machine actor through a coincidentally matching pool slot. Newly acquired client actors retain their own local slot; snapshots preserve a correctly bound actor when only the server slot number differs. Delayed demotion and physics updates also require the matching live ISID before acting on a candidate. Missing product definitions defer selection until the client can determine the correct class.

The repair preserves product-driven visuals, pool reuse for compatible inactive actors and the machine's existing authored geometry. It does not hide the symptom by forcing every client pedestal to a cube.

Automatic pool shrinking in `ASMComponent.cpp` also excludes externally owned scene actors. Previously, registering a placed machine as an empty slot could make it eligible for destruction when a new prop was acquired. Only inactive, unbound actors owned by the pool manager are now removed by automatic shrinking.

## Validation

Editor **18103** builds; **70/70** selected native tests pass (51 clean, 19 with warnings). The focused collision regression verifies original actor survival, mesh/material/scale, forward and inverse pool mappings, occupied slots, delayed definitions, repeated snapshots, and both rejected and valid demotion/physics updates. Evidence: `Saved/ColaContainer_20261118/Final_18103/Report/index.json`.

Read-only asset evidence: `Saved/ManualMachineVisualAudit.json`. Separate host/client/late-join verification **passed** on module **18103**, with all three processes exiting successfully and an unchanged module manifest. Both the placed and runtime-spawned machine retained `Cube`, `WorldGridMaterial`, scale 1 and the correct class/product/instance identity on both clients. Evidence: `Saved/ManualMachineVisual_20261118/Network_022444_012300/result.json` and each peer's `*_passed.json`.

The opt-in `ManualMachineVisualTest` fixture uses isolated user directories. `Scripts/CreateManualMachineVisualMap.py` creates `/Game/__CodexMachineVisualCheck/Lvl_ManualMachineVisual` with the real saved machine Blueprint and product definition, a native manager, the fixture and an engine-cube floor. The original prototype maps are unchanged. `Scripts/RunManualMachineVisualNetwork.py` runs the comparison and records the exact module manifest and each peer's mesh/material/scale and logical identity. These are real network processes using NullRHI; rendered frames were not captured. The first attempt on the full validation map timed out before gameplay while preparing unrelated high-detail artwork, so the passing run used the isolated map.
