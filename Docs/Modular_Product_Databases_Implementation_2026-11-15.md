# Modular product databases — implementation and checkpoint

**15 November 2026 — installed in ColossalFizzCo.** This report covers the designer's modular Bottle / Content / Cap update and the registration bug that originally made shelf bottles unresponsive. It does not mark the whole game or a new packaged build complete. The earlier numbered DLLs, packaged build 14120 and historical reports are preserved.

## What changed

| System | Installed behavior | Validation and practical limit |
| --- | --- | --- |
| Shared product definitions | Three typed databases, bottle capacity/physics, capacity-free content assets, cap assets, identity-only catalog | Native catalog and real asset validation pass |
| Stable PID authoring | Automatic allocation for new catalog entries, issued-ID reservation, rename/reorder stability, named category-filtered searchable references, validation warnings/errors | Identity regression and editor picker construction/filter/search pass; manual editor appearance pending |
| Bottle quantity | Maximum belongs to the shared bottle type; current portions belong to its runtime record; initial fill remains separately configurable | Four- and eight-portion fixtures, all five real Normal recipes, save/load and replicated capacity changes pass |
| Filling | Selected Content PID validates against Bottle PID; per-bottle liquid geometry/materials/mount; one child for repeated filling; empty bottles can receive a different compatible cola | Positive and rejected combinations pass without rejected-attempt mutations |
| Physical caps | Bottle PID compatibility checked before install/manual rotation; attach/remove maintains both owner and child links; cap ISID retained | Cap rejection, attachment, sealing, removal and exact-pose transition regressions pass |
| ASM / ISM | Child interaction resolves root; assembly reconstructed on one active actor; persistent resting slots parked/restored; individual identities retained | Native lifecycle plus actual host/client/late join pass |
| Save restoration | Schema6 writes exact hierarchy/poses; older tagged-save default remains 5; two-phase restoration prevents children being generated before saved IDs exist | Fresh-manager disk regression and both actual save copies pass |
| Shop integration | Procurement uses typed products; old initial-stock presets resolve through canonical bottle identity; NPCs request content/capacity; scanner distinguishes contents under one bottle identity | Full native game regression passes; fresh human input loop after this change still pending |
| Rejection feedback | Local requesting player receives red top-right “Content not supported” or “Cap not supported”; no mutation on rejection | Result paths integrated; rendered position/readability pending |
| Effects / cost | Optional local pooled Niagara/Cascade effects for active content; resting ISMs do not run effect components; bounded existing delivery channel reused | Non-editor game compiles; real replication passes; representative CPU/GPU/network profiling pending |

All implementation is native C++ plus data assets. No Blueprint graph, map placement, character size, bottle size or artist mesh pivot was rewritten for this update. The Normal-only plastic/manual-twist prototype scope remains.

## Root causes and fixes

1. **Registration failed for the whole Normal catalog.** `DA_NormalSpicy` had 8 portions while the old procurement path required every flavor to match the empty bottle's 4. That aborted initialization, so otherwise visible stock could lack successful manager registration. The typed path reads one canonical bottle capacity and treats the flavor assets as initial-content presets. Failed first registrations also roll back their provisional ISID safely rather than leaving an unusable visible actor.
2. **The old flavor SKU model conflicted with modular ownership.** Filling previously selected a finished-bottle product. It now retains the original bottle PID/ISID and physical state, updates the selected content identity and adds/updates one child. Content visuals read the owning bottle's authoritative fill. Empty bottles retaining an old content ID no longer reject a different compatible fresh cola.
3. **Named initial content needed a runtime bridge.** The legacy data-asset conversion read only the old integer content ID. It now resolves `InitialContentProduct` first, with legacy ID fallback. The fill regression uses the named reference with the old field unset.
4. **Fresh-manager restore order could duplicate children.** Refreshing each row as it loaded could allocate an assembly child before later saved rows were restored. The loader reserves the full saved ID range, restores every row without early assembly generation, then rebuilds the graph. Schema6 avoids applying old cap-offset corrections to newly saved exact poses.
5. **Capacity edits could create liquid.** Preserving normalized fill alone would turn 2/4 portions into 4/8 after a shared capacity edit. Definition updates and save restoration now conserve the original amount before calculating the new fraction. The replicated client path uses the same conversion.

## Authoring guide

Use `/Game/Props/Products`:

- `DA_ProductCatalog`: readable names and stable identities.
- `DB_Bottles`, `DB_Contents`, `DB_Caps`: references to the three definition types.
- `DA_Bottle_Plastic`: **Maximum Portions**, currently 4, plus bottle physical/pressure settings.
- `DA_Content_Lemonade`, `DA_Content_Mint`, `DA_Content_Sweet`, `DA_Content_Spicy`, `DA_Content_ZeroCalorie`: liquid properties and supported-bottle visual recipes. Each recipe selects a bottle by name and supplies its liquid geometry, materials, optional effects and relative transform.
- `DA_Cap_NormalTwist`: cap geometry/settings and **Supported Bottles** by name.

The old `/Game/NormalColaPrototype/DA_Normal*` bottle assets remain initial-stock presets for existing levels/saves. Their `Bottle Product`, `Initial Content Product` and `Initial Cap Product` references resolve the modular definitions. Their old quantity field no longer sets the canonical Plastic Bottle capacity. To make initial stock half full, set its **Default Content Level** to 0.5. Current saved instance data takes precedence when restoring a game; changing an authoring default does not erase saved state. BP_Cola's saved debug random content option remains disabled.

To add a product, create its appropriate typed asset, add it to the matching database, then use **Register Database Products** on the central catalog. A new catalog row gets a PID automatically; edit its readable name and save the catalog. Configure content/cap supported bottles using the filtered picker. Keep issued IDs stable; do not delete and recreate a product to rename it. Use asset validation to detect invalid definitions/references. Compatibility PIDs stay in shared definitions; OwnerISID/ChildISIDs belong only to runtime records.

Current actual primary catalog PIDs are Bottle 1000, Cap 1001 and Contents 901–905. These numbers are recorded for debugging, not a requirement to enter raw IDs. Existing world-local saved product IDs are preserved and resolved using asset-based compatibility aliases. Old save slots used overlapping numeric namespaces, so they were not globally renumbered.

## Multiplayer and performance

The server validates compatibility, filling, cap state and ownership mutations. Immediate owner edges remain canonical; the direct-child cache and current-portion value are rebuilt locally instead of sending duplicate hierarchy arrays or another quantity stream. A small `VisualBottlePID` selector lets one content product use several bottle-shape ISM batches.

The existing delivery channel retains 896-byte definition chunks, at most 2 instance records per message, at most 2 messages per peer per manager tick and at most 8 unacknowledged messages. Runtime mutation/representation data uses the established dirty/versioned paths. No unbounded all-products RPC or per-frame updates for every inactive bottle were added. These bounds reduce burst size; they are not a measured bandwidth/FPS guarantee. All peers must use the updated build; the prototype compatibility ID is `normal-cola-prototype-modular-v6`.

## Verification results

| Check | Result | Evidence |
| --- | --- | --- |
| UE5.8 Editor C++ build | Passed | `BuildReferenceRefill.log` |
| Non-editor Win64 Development game build | Passed | `BuildGameFinal.log`; compile/link validation only |
| Final staged native game/editor tests | 72/72 pass: 66 clean, 6 with warnings; 0 failed/0 not run | `../PortionBug_20261115/ModularFinalFullResult.json` |
| Installed ColossalFizzCo native tests | 72/72 pass; original saves unchanged | `InstalledValidation.json` |
| Actual prototype placed-stock audit | 366 Normal bottles register, validate and use canonical capacity/content children | `PrototypeValidation.json`, `PrototypeAudit02.log`; no map save |
| Local IP listen server, separate client and late join | All three processes exit0; assertions pass | `NetworkResult.json`, `Network_143148/` |
| Existing ShopAutosave copy | 720/720 instances restored, 360 original roots retained, no hierarchy errors or pose changes | `SaveCopyValidation.json` |
| Existing NormalColaPrototypeAutosave copy | All 3945 original roots retained, 10832 restored records, no hierarchy errors or pose changes | Same file; 216 obsolete empty-liquid child records intentionally retired, no unexpected losses |
| Clean real-save validation process |Exit0 | `SaveValidationFinal.log`; unrelated local MCP listeners disabled for this commandlet |

The network test checks compatible filling, current portions, per-bottle content representation, attachment, a cap interaction promoting the whole bottle, held movement, genuine release/demotion, late join and cap removal. It also changes the shared capacity 4→8 and verifies that an existing 2-portion bottle remains 2 portions/25% full on the host and both clients. It is a headless real connection test, not just role flag simulation. It is not a new Epic-service or cross-network acceptance run.

The six warning-bearing native tests retain known fixture collision/physics and ABP_Unarmed animation thread-safety warnings. They pass assertions; the suite is not warning-free. The gameplay filter does not rerun every UniversalCrossplay plugin test. That plugin's earlier real-service evidence remains historical and unchanged by this update.

## Saved progress and deployment

Changes were validated in `MarryByMidnight/Saved/ModularWork`, then installed into the authorized `ColossalFizzCo` project with hash guards and backups of only the deployment whitelist. Original progress files were not migrated in place or deleted. Save compatibility was checked using copies. Installed tests preserved all three exact originals, with no test-modified original requiring restoration:

| Original save | SHA-256 after validation |
| --- | --- |
| ShopAutosave.sav |`048bf519881c09f6d55a24b6cf8adaf09c4f90b590c1ba53c4e96b3a478d6cdb`|
| NormalColaPrototypeAutosave.sav |`8fdc4c1679ac52440ab105a5890ac4dfa61fb26b7af7bae61fb2dc6de3bb0995`|
| NPC_CulturalKnowledge.sav |`e7adf45d7f9f0ef09bf81407475c9cda15d40201b65bba4c301e674c775b41d1`|

Deployment backups are `BeforeLiveDeployment_141649` (initial), `_143756` (quantity conservation), and `_144812` (named-reference/empty-refill completion), under `MarryByMidnight/Saved/ProductCatalog_20261115`. The installed editor modules are `UnrealEditor-ColossalFizzCo.dll` and `UnrealEditor-ColossalFizzCoProductEditor.dll`, engine BuildId 55116800. The isolated CrossplayChecks QA plugin/module is not installed. Existing source-control changes predating this task are retained; no blanket Git staging, commit, reset or push was performed.

ProjectVersion is 0.1.6, product assets are included in the cook directory settings, and private local Epic profiles retain their credentials with only their compatibility ID updated. Secrets were not added to tracked public config. The existing launcher remains the entry point: `Play_ColossalFizzCo.cmd`.

## Remaining acceptance work and next steps

1. Open the updated project and run the physical-input sequence: pick up an empty bottle, select each Normal cola, fill, cap, stock, NPC pickup, scan/pay/change, finish sale and ATM/reinvest. Existing native loop tests pass; a fresh human/rendered loop on this revision has not been accepted.
2. Inspect rendered ASM/ISM liquid levels, per-bottle transforms, the named picker and the exact red notifications. Desktop inspection failed to initialize (`failed to write kernel assets`, Windows path error3), so this session cannot honestly claim visual acceptance.
3. Cook/package this revision and repeat the loop and late join in that packaged build. The Development game target compiles, but the old packaged 14120 candidate is unchanged and does not contain this update.
4. Repeat assembly interactions through Epic P2P on two machines/different networks; exercise reconnect and adverse network conditions. Current new network evidence is local-IP/listen-server/headless.
5. Profile large resting inventories and active promotion bursts with representative hardware. Optional artist-supplied effects need visual tuning if configured. No measured frame-time/network budget acceptance is claimed.

No additional design approval is needed for the completed modular update. Incompatible-content failure events, other bottle materials/caps/cola categories and raw-ingredient recipes remain deliberately deferred by the approved prototype scope. Existing unrelated animation warnings remain outside this change.

## Installed source and asset inventory

The following list records this update, not all previously dirty project files. Runtime changes live under the existing game module; the picker/migration helpers live in the editor-only module.

- `Source/ColossalFizzCo/ColossalFizzCo.Build.cs`
- `Source/ColossalFizzCo/Private/Components/InteractableInstancedStaticMeshComponent.cpp`
- `Source/ColossalFizzCo/Private/Props/InteractablePropDatabase.cpp`
- `Source/ColossalFizzCo/Private/Props/InteractablePropManager.cpp`
- `Source/ColossalFizzCo/Private/Props/PropAssembly.cpp`
- `Source/ColossalFizzCo/Private/Props/PropCapAssembly.cpp`
- `Source/ColossalFizzCo/Private/Props/PropCatalogRuntime.cpp`
- `Source/ColossalFizzCo/Private/Props/PropLiquidTransfer.cpp`
- `Source/ColossalFizzCo/Private/Props/PropManagerState.cpp`
- `Source/ColossalFizzCo/Private/Props/PropProductCatalog.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopCustomerCashComponent.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopCustomerVisitComponent.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopManualCapping.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopManualFilling.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopPlayerInteractionComponent.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopProcurementComponent.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopProductionNotice.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopProductionPlayerActions.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopProductionStationComponent.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopScannerVolume.cpp`
- `Source/ColossalFizzCo/Private/Shop/ShopStockQueries.cpp`
- `Source/ColossalFizzCo/Private/Tests/BottleContentDynamicsTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/BottleLoadLifecycleTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/DefaultBottleCapTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/ModularProductCatalogTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/NormalColaFlavorFillingTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/PropInitialContentDebugTests.cpp`
- `Source/ColossalFizzCo/Private/Tests/ShopManualCappingTests.cpp`
- `Source/ColossalFizzCo/Public/Components/InteractableInstancedStaticMeshComponent.h`
- `Source/ColossalFizzCo/Public/Props/InteractablePropDataAsset.h`
- `Source/ColossalFizzCo/Public/Props/InteractablePropDatabase.h`
- `Source/ColossalFizzCo/Public/Props/InteractablePropManager.h`
- `Source/ColossalFizzCo/Public/Props/PropProductCatalog.h`
- `Source/ColossalFizzCo/Public/Props/PropProductReference.h`
- `Source/ColossalFizzCo/Public/Shop/ShopCheckoutTypes.h`
- `Source/ColossalFizzCo/Public/Shop/ShopCustomerCashComponent.h`
- `Source/ColossalFizzCo/Public/Shop/ShopCustomerVisitComponent.h`
- `Source/ColossalFizzCo/Public/Shop/ShopPlayerInteractionComponent.h`
- `Source/ColossalFizzCoEditor.Target.cs`
- `Source/ColossalFizzCoProductEditor/ColossalFizzCoProductEditor.Build.cs`
- `Source/ColossalFizzCoProductEditor/Private/PropCatalogMigrationTools.cpp`
- `Source/ColossalFizzCoProductEditor/Private/PropProductReferenceCustomization.cpp`
- `Source/ColossalFizzCoProductEditor/Public/PropCatalogMigrationTools.h`

- `Content/NormalColaPrototype/DA_NormalEmptyBottle.uasset`
- `Content/NormalColaPrototype/DA_NormalLemonade.uasset`
- `Content/NormalColaPrototype/DA_NormalMint.uasset`
- `Content/NormalColaPrototype/DA_NormalSpicy.uasset`
- `Content/NormalColaPrototype/DA_NormalSweet.uasset`
- `Content/NormalColaPrototype/DA_NormalZeroCalorie.uasset`
- `Content/Props/Products/DA_Bottle_Plastic.uasset`
- `Content/Props/Products/DA_Cap_NormalTwist.uasset`
- `Content/Props/Products/DA_Content_Lemonade.uasset`
- `Content/Props/Products/DA_Content_Mint.uasset`
- `Content/Props/Products/DA_Content_Spicy.uasset`
- `Content/Props/Products/DA_Content_Sweet.uasset`
- `Content/Props/Products/DA_Content_ZeroCalorie.uasset`
- `Content/Props/Products/DA_ProductCatalog.uasset`
- `Content/Props/Products/DB_Bottles.uasset`
- `Content/Props/Products/DB_Caps.uasset`
- `Content/Props/Products/DB_Contents.uasset`

Additional project/config edits: `ColossalFizzCo.uproject`, `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`, and the ignored local Epic/EpicPrototype compatibility profile fields. Four editor DLL/PDB files were installed. Native testing/build/migration/network runners and their evidence stay under the saved checkpoint directories, not the shipping game.
