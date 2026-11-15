# Colossal Fizz Co — technical audit

Audit date: 15 November 2026, Asia/Calcutta. Project: `ColossalFizzCo.uproject`, Unreal Engine 5.8, Win64.

## Decision

**A substantial, connected first-playable candidate exists. It is suitable for controlled internal playtesting, but the available evidence does not establish a stable, release-ready prototype or a complete game.** Manufacturing, stocking, NPC shopping, physical payment, ATM banking and reinvestment are implemented and have automated integration evidence. The present frontend supports offline play and an Epic session workflow.

- **Current manual-filling prototype: approximately 84% complete; reasonable range 80–90%.** This includes implementation, integration and acceptance work, rather than counting source files.
- **Prototype against every still-written GDD requirement: approximately 75–85%.** Practical cooling and the complete physical hot-source/hose/spray workflow remain incomplete or unaccepted. The GDD's mandatory two-person sealing requirement conflicts with the later solo-capable specification.
- **Complete game: approximately 28%; reasonable range 20–35%.** Shared technical foundations are advanced; most production equipment, supernatural content, logistics, progression and final presentation are absent or only foundations.

These are engineering estimates, not measured facts or percentage-of-code metrics. Test pass rates are not completion percentages. A precise full-game percentage is impossible while machine variants, content counts, level scope, platform commitments and several mechanics remain undefined.

## Evidence and audit coverage

Fresh checks performed against the installed project:

| Check | Actual result | Meaning and limit |
| --- | --- | --- |
| Native gameplay automation | **67/67 pass: 61 clean, 6 with warnings; 0 failed, 0 unrun** | Executed installed Editor binaries, unattended, NullRHI, isolated audit UserDir. Does not verify graphics, hardware controls or a newly rebuilt source tree. |
| UniversalCrossplay automation | **5/5 clean passes**, 0 failed/unrun | Native plugin behavior; does not independently prove online service, audio or storefront acceptance. |
| Current asset registry | **2,175 unique /Game packages** | Includes external actors/objects; this is not 2,175 distinct gameplay features. |
| Blueprint compilation in memory | **118 assets: 116 clean, 1 warning, 1 error** | Ordinary Blueprint, animation Blueprint and widget Blueprint types. Six ControlRigBlueprint assets are inventoried separately and are not covered by this compile count. |
| Asset immutability | **2,175 package SHA-256 hashes unchanged** during the broad asset audit | No content package was saved by the audit. |
| Data tables | **0 /Game DataTable assets found** | Runtime data uses native registries, product DataAssets and a supply catalog; no missing table rows can be inferred. Plugin/engine tables were not exhaustively audited. |
| Product/configuration assets | **46 InteractablePropDataAssets, 1 ShopSupplyCatalog, 1 FragmentAssemblyDataAsset** | Loaded product properties and catalog source inspected. Configuration presence alone does not establish gameplay. |
| Core Blueprint graph export | **168 graphs from 12 selected gameplay Blueprints** | Current bottle, player, game mode, scanner, checkout, ATM, bucket, NPCs, tank and cargo adapter graphs exported. Graph DSL is a structural aid, not a full runtime proof. |
| Native inventory | **234 .h/.cpp/.cs files** across the game and crossplay source | Indexed and hashed; semantic review concentrated on the active gameplay paths, shared state, persistence, RPCs, tests and integrations. Not a formal line-by-line proof of every function. |
| Frontend deployment provenance | **22/22 deployment entries match recorded hashes** | Matches the frontend installation manifest; not a provenance proof for every historical binary. |

Fresh evidence: `Saved/Audit20261115/Automation/index.json`, `PluginAutomation/index.json`, `AssetAudit.json`, `NativeInventory.json`, `BlueprintGraphs/Index.json`, and the associated Tests/PluginTests/Assets/Graphs logs. The broad asset commandlet exited **1** after recording the excluded food-container compile error and legacy member-resolution ensures; this is **not** a clean whole-project validation. The gameplay and plugin automation processes exited 0.

Historical raw evidence was also inspected: the manual-filling 8-check network result, restored-bottle 14-check network result, frontend regression summary/checkpoint and deployment manifest. Other packaged/multiplayer claims below are explicitly identified as historical records. No new packaged build, two-account session, rendered playthrough, network soak or clean C++ rebuild was performed in this audit. Existing successful build logs and installed-binary execution support buildability, but do not prove that every current source file rebuilds cleanly from scratch.

Primary specification sources: `GDD_Master.md`, `Core_Gameplay_Design_Decisions.md`, `Current_Development_Scope.md`, scanner/cash design updates, architecture documents, `Normal_Cola_Manual_Filling_Design.md`, and dated implementation records through `Prototype_Menu_Implementation_2026-11-15.md`. DOCX checkpoint pagination/visual presentation was not validated; the corresponding Markdown records were used for technical status.

## Specification reconciliation

1. The latest manual-filling design explicitly allows one player to complete production, natural venting, temporary protection, and multiple purchased machines. The older GDD/technical-plan headers still require two separate capping operators. Current code has `bAllowSoloCap`, residual-pressure/temporary-protection gates and optional cooperative roles. **Do not treat solo operation as a newly discovered bug. Update the authoritative GDD before acceptance.**
2. The approved prototype flight choice is the original Rocket Projectile Component, with Chaos thrust retained as an alternative. Original flight is not a failure merely because it is not continuous Chaos propulsion.
3. Preserve current player/NPC/bottle scale. Obsolete 25 cm player targets, 80 kg bottle tuning, unconditional eruption countdowns and older pricing drafts are not active acceptance criteria.
4. The manual machine has a conditional three-second temporary-seal failure rule from the later design. This is distinct from the superseded universal uncapped eruption countdown.
5. Plastic bottles, five Normal flavors and Normal manual twist caps define the current slice. Other materials, 16 additional cola types, advanced closures and powered equipment belong to full-game scope.
6. Eleven named legacy PSOs are explicitly excluded by `Current_Development_Scope.md`. Their broken references remain real repository facts, but repair is not a prototype gameplay blocker. They must still be considered when selecting a cook scope.
7. Older reports describing builds11193/11194 or missing frontend/manual production are historical. Installed frontend records describe BuildMenu12 over the repaired gameplay lineage14120; the preserved packaged14120 does not automatically contain the new menu.

## System progress summary

Percentages estimate completion of the stated scope, including validation and integration. “Fully Implemented” applies to bounded subfeatures below; broader systems remain partial when acceptance or content is missing.

| Major system | Category | Estimate | Verified scope / principal remaining work |
| --- | --- | ---: | --- |
| Prop identity, shared/unique state, ASM/ISM and pooling | Partially Implemented | 90% | State/pool/persistence/activation regressions pass; hierarchy and snapshots have network evidence. Large-stock performance, long reuse soak and failure recovery remain. |
| Plastic bottle handling, physical mass and liquid visuals | Partially Implemented | 85% | Grab routing, liquid continuity, variable mass and lifecycle tests pass. Hardware feel, all visual effects, collision warnings and edge cases remain. |
| Pressure, agitation, cap ejection and rocket lifecycle | Partially Implemented | 85% | Thresholds, motion pressure, discharge, reuse and restored lifecycle tests; historical lag/loss peers agree. Designer tuning and broad physical/remote acceptance remain. |
| Modular shell/liquid/cap attachment | Partially Implemented | 90% | Persistent child identities, compatibility, hierarchy validation, cap mounting and ASM/ISM reconstruction tested. Arbitrary future parts/content and large assembly stress remain. |
| Five Normal contents and pricing | Partially Implemented | 90% | Five-flavor filling and pricing tests pass; provisional balance and complete visual/content approval remain. |
| Manual filling and optional cooperative sealing | Partially Implemented | 85% | Lever/lease, accepted transfer, temporary seal, venting, solo/two-role closure and purchased machines tested. Complete hardware session and authored presentation remain. |
| Physical hot-source, flexible hose/nozzle and spray | Partially Implemented | 30% | Tank/cable adapters, linked source and visible pipe exist. No accepted complete high-pressure free-spray/hose gameplay pipeline. |
| Temperature model and playable cooling station | Partially Implemented | 40% | Thermal state, mixing, batch updates and price discount exist. Practical cooling workflow, hot-output tuning and persistence policy acceptance are incomplete. |
| Shelf stock, NPC shopping/queue/departure | Partially Implemented | 85% | Claims, real stock selection, buckets, route, tender and retirement implemented; historical repeated-sale evidence. Crowding, blocked routes, human pacing and long sessions remain. |
| Scanner, locked invoices and expiry adjustments | Partially Implemented | 95% | Live/saved entries, reservations, grouping, pricing and compensation/debt regressions pass. Broad interaction acceptance and adversarial/soak cases remain. |
| Physical tender, finite change, ATM and shared bank | Partially Implemented | 95% | Conservation, exact change, duplicate/atomic failure paths, banking and withdrawal covered by passing tests. Long shared multiplayer economy soak remains. |
| Procurement, delivery and reinvestment | Partially Implemented | 90% | Catalog, purchase/carry, supply debits and reinvestment tests pass. Broader delivery/logistics and balance remain. |
| Save/load and autosave | Partially Implemented | 85% | Canonical bottle/hierarchy, wallet/cash/debt and restored lifecycle tested. Crash recovery, corruption handling, long-history size and full migration coverage remain. |
| Core gameplay replication | Partially Implemented | 85% | Authority gates, owned action RPCs, delivery acknowledgements, late snapshots and historical lag/loss tests. Four-player sustained production and different networks remain. |
| Offline/Epic frontend and session lifecycle | Partially Implemented | 85% | Fresh widget/plugin tests; historical real two-account menu/travel/cleanup acceptance. New package, different-PC/network and manual friend-row checks remain. |
| Steam/Epic cross-store matrix and audible voice | Partially Implemented | 35% | Provider/RTC/session integration exists; Steam combinations, audible RTC and overlays unaccepted. |
| Generic NPC cognition | Partially Implemented | 40% | Needs/personality/planner/world-model/knowledge components exist. This is not proof of complete patron preferences, anger or supernatural reactions. |
| Glass damage/fragments and other vessel mechanisms | Partially Implemented | 30% | Glass fragment lifecycle code/assets/tests exist; full material progression, wooden stoppers, auto-lock/release variants and welding are incomplete. |
| Specialized factory vehicles/logistics | Partially Implemented | 10% | Chaos vehicle templates/cargo assets exist. Forklift docking, secured cargo, mobile capper and their networked production loop are unverified/missing. |
| Advanced machinery and energy infrastructure | Not Implemented | 0–5% | No complete Cap Gun, automatic filler/capper, pressure regulator, cleaner, voltage distribution or coal-generator gameplay found. Generic data/engine plugins do not complete them. |
| Hazardous cola categories and countermeasures | Not Implemented | 0–5% | Enums/design/legacy names are foundations; no accepted toxic/spider/radiation/magic/explosive gameplay loop. |
| Shift quotas, ratings, rent/bankruptcy and campaign progression | Not Implemented | 0–5% | GDD concepts; no complete progression controller or accepted win/loss loop found. |
| Full-game art, levels, audio, onboarding and release QA | Partially Implemented | 15% | Prototype environment, meshes, animations and UI exist. Complete art/content inventory, final audiovisual feedback, accessibility and platform readiness are missing. |
| Excluded PSO_FoodContainer | Broken, excluded | N/A | Fresh missing/NULL parent compile error. Do not include in active feature completion. |

## Fully implemented bounded features

The following have implementation plus passing automated coverage. “Confirmed” means the checked test cases passed, not universal correctness or production certification.

- **Pricing rules:** fractional fill clamped to nominal value, symmetric temperature discount capped at20%, no overfill premium, cents retained until aggregate whole-dollar half-up rounding. `ColaPricingLibrary.cpp` and pricing tests implement the current economy rules.
- **Checkout transaction rules:** unique ISIDs, reservations, immutable locked receipt, exact change requirement, stock ownership transfer, duplicate-completion rejection and stale-quote checks. Amount-only compatibility payment APIs deliberately reject requests: physical coins are required, so these are not unfinished stubs.
- **Cash/bank separation:** completed sales retain actual register coins; ATM deposit credits bank and retires/banks those coins. Withdrawal/deposit conservation, finite drawer exchange and failure paths have passing tests.
- **Post-sale expiry accounting:** once-only base-price obligation, fresh compatible replacement or additional deduction, separate adjustment history and unpaid debt persistence/recollection. This is not the old “reject every expired product” behavior.
- **Core liquid transfer accounting:** fractional accepted quantities, source debit, compatible target validation, resumed fill age, temperature mixing and shared-source conservation. Unaccepted liquid is not blindly charged to the source.
- **Managed cap identity and basic compatibility:** persistent cap/bottle relationships, attachment/detachment, manual closure and child identity restoration. This completes Normal twist-cap primitives, not every future closure type.
- **Normal content coverage:** all five configured flavors have a passing filling integration test. Native registries and the supply catalog are the relevant runtime data layer; a spreadsheet/DataTable backend is not required for this to work.

Player-facing completion remains conditional on hardware/rendered acceptance. There is no defensible claim that an entire broad system is 100% production-ready simply because these primitives pass.

## Integration findings

**Bottle → pressure → flight:** `ColaPressure.cpp` derives pressure from sealed agitation plus content contribution, reduces tolerance with Strength, and evaluates a strict greater-than threshold. Fully sealed caps launch through the original or optional Chaos implementation; temporary manufacturing failure has its own residual-pressure condition. The server releases a held bottle, ejects its managed cap, clears seated state and starts flight. Distance-driven original-flight discharge updates remaining liquid and pressure before falling handoff. Empty/open/held cases are intentionally distinct, not one unconditional launch rule.

**ASM/ISM → modular assemblies:** `PropManagerState.cpp` publishes canonical changes to active actors, assembly components and ISM custom data. `ColaContainment.cpp` prevents premature demotion while liquid/pressure/partial attached caps require active simulation. Binding/reset code clears pooled transients and preserves incoming remote flight state when replication arrives before identity binding. Saved parent edges reconstruct local children. Missing parents/cycles/depth64 are rejected at load. The historical cap-on-floor and frozen reloaded bottle problems have targeted repair tests and later network evidence; do not relist them as currently reproduced failures.

**Manufacturing → finished stock:** `BottleFillingComponent.cpp` uses bounded fixed transfer steps and authority checks. `ShopManualFilling.cpp` validates source range, bottle alignment, exclusive lever ownership and lease expiry; moving a machine stops filling and releases its seated bottle. `ShopManualCapping.cpp` stores partial closure canonically, supports optional separate roles and safely permits solo rotation after venting or during temporary protection. `ShopStockQueries.cpp` rejects open bottles in the active Normal prototype stock path and rechecks changed products.

**Stock → customers → checkout:** `ShopCustomerFlow.cpp` and `ShopCustomerVisitComponent.cpp` implement stock claims, giant navigation, bucket allocation/carry/dock, queue spacing, request preservation, checkout waiting, return and visit retirement. Sold physical goods retire while financial history remains. These native paths coexist with older Blueprint AI/planner systems: legacy hunger/cooking plans are not proof of the complete commercial patron simulation.

**Checkout → ATM → procurement:** invoice validation uses canonical product data; physical payment commits to the register. The shared wallet is used by ATM and purchases, rather than crediting sales twice. Expiry debt collection is deferred safely around wallet restoration. Procurement loads a five-product Normal catalog, registers compatible caps/contents and retains legacy identities while adopting the current assembly recipe.

**Multiplayer:** owned player action components validate authority, reach, session and sequence; the manager uses per-controller delivery channels with reliable acknowledged snapshots and coalesced changes. Definitions are chunked; active motion and continuous data use separate paths. These are strong foundations, but message bounds are not measured aggregate bandwidth. Historical 75ms/2% loss tests and >1,024-ID snapshot coverage do not establish arbitrary capacity, WAN stability or every four-player interaction.

**Temperature:** actual transferred volume drives temperature mixing, and both active/instanced state participate in thermal updates. This integrates pricing with bottle state. It does not supply an accepted chilling station, ice economy, freely handled hot hose or an approved offline thermal policy.

## Bugs, broken assets and risks

| Priority | Finding | Evidence / confidence | Consequence and next action |
| --- | --- | --- | --- |
| P1 | Current frontend is not in a freshly accepted standalone package | Latest frontend record explicitly launches Editor in game mode; old14120 package preserved | Blocks shipping this exact candidate. Build/cook/package current code, include menu/shop and runtime soft assets, then test packaged peers. |
| P1 | Human production/service acceptance incomplete | Native tests and network drivers invoke actual services/RPCs/widget callbacks; full hardware two-player loop remains pending | Cannot certify control feel, targeting, station reach, interrupted inputs or visual readability. Run a complete operator session. |
| P1 | Below-world bottle records require investigation | Bottle lifecycle repair record reports out-of-world saved records; not freshly reproduced here | Possible inaccessible/lost stock. Reproduce from copied saves and define recovery/retirement without erasing history. Do not confuse intentional parked pool actors at Z=-100000 with lost live stock. |
| P1/P2 | GDD and latest manufacturing acceptance disagree | Mandatory dual operators versus explicit solo-capable later design | Avoid implementing the wrong prototype. Reconcile scope before calling remaining requirements complete. |
| P2 | Physics simulation/collision warnings persist | Fresh Heavy, Lightweight, PlasticPressure and ManualWheelRotator tests | Tests pass, but the fixtures request simulation with incompatible collision settings. Determine fixture-only versus real actor transitions; correct and rerun affected physics tests. No fresh evidence that all production bottles fail physics. |
| P2 | ABP_Unarmed has potentially thread-unsafe animation call | Fresh compile: Two Way Blend → Get Punching State | Actual warning, not a reproduced crash. Cache safe state or use a verified thread-safe access path; test animation with parallel updates. |
| P2 | Legacy migration test emits missing OldShell object warning | Fresh LegacyPlasticBottleMigration test; temporary /Temp/PropSaveRedirect path | Test passes its migration assertions. Classify expected diagnostic versus incomplete redirect; do not call it general live shell corruption. |
| P2 | Renderer/material and tool startup warnings have historical records | Earlier rendered logs document translucent/Nanite warnings and Python toolset startup errors | Current NullRHI passes cannot settle visual validity. Reproduce on current D3D12/package and inspect shell/liquid/cap rendering. |
| P2 | Persistence durability/performance not accepted | Synchronous SaveGameToSlot; growing receipt/released-liquid histories; exact schema validation | Crash/truncation recovery, save-size growth, long sessions and frame stalls need measurement. These are risks, not reproduced corruption claims. |
| P2 | Large active-stock/network performance unknown | Pools, batched state and bounded delivery exist, but no current target-hardware profile | Measure frame time, active actor count, snapshot time, memory and bytes/sec under busy four-player production. |
| P2 | Real storefront/audio matrix incomplete | Plugin validation explicitly leaves Steam combinations, audible RTC and separate networks pending | Do not advertise universal crossplay/working voice from native tests alone. |
| P3 / excluded | PSO_FoodContainer missing parent and related member-resolution ensures | Fresh whole-content compile; commandlet exit1 | Broken legacy package. Preserve scope exclusion; audit production cook dependencies rather than repairing the abandoned food game. |

No active non-excluded Blueprint compile error was found in the 118-asset set. After excluding the eleven PSOs, **107 remain:106 clean and1 warning**. No new C++ compilation error is established by this audit; a fresh full source rebuild was not attempted. SDK validation reports Win64 valid and several other target SDKs absent: that is an environment/platform limit, not evidence that the Win64 game fails compilation.

The working tree already contains extensive modified and untracked implementation/assets, including plugin/content/source. This audit does not commit, revert or repair them. Release reproducibility requires reviewing and capturing a coherent candidate; neither a clean Git checkout nor a complete source-to-binary identity proof is established.

## Missing full-game features

- Sixteen non-Normal contents across Toxic, Black Magic, Radioactive and Explosive categories, approved properties/balance, effects and hazards. A legacy FireCola asset or hazard enum does not implement this catalog.
- Player countermeasures and consequences: gas exposure/visibility, swarms, radiation propagation, magical effects, cleanup, equipment and cooperative recovery. Exact older named effects remain proposals where superseded.
- Complete material/closure progression: glass wooden stopper/pressure release, plastic automatic variants, welded metal lids, appropriate tools, opening/reuse and tuned failure rules. Existing glass fragments are only part of this work.
- Powered/fuel equipment: Cap Gun, automatic filling/capping variants, pressure regulator, automatic cleaner, voltage/power/pressure operating rules and generator infrastructure.
- Purpose-built carts, forklift hoisting/docking, secured transport crates and mobile capper interactions; template driving is not the required factory logistics game.
- Practical chilling/ice supply, hot-source handling, complete flexible hose/dispenser interaction and spray/spill consequences. Released-liquid accounting already exists; puddle simulation, slowing, cleanup and free-spray capture are separate.
- Expanded patron roster, preferences, reactions, patience/anger, reputation and destructive tantrums. Generic needs/personality/ragdoll classes are useful foundations.
- Shift scheduling, quotas, ratings, rent, bankruptcy/win/loss, upgrades/unlocks and campaign/replay progression. Raw-ingredient manufacture remains later scope.
- Return/washing/reuse infrastructure and broader recycling presentation; the prototype's deliberate sold-goods retirement is not a broken return system.
- Complete themed environments, machinery art, animations, VFX, audio, tutorials, configurable controls/accessibility and platform-specific UX.
- Production platform work: full Steam/Epic matrix, audible voice, release packaging, target-machine performance, localization scope, console SDK/platform integration and certification if consoles remain committed.

## Completion estimate method

The current prototype estimate weights its deliverable rather than giving all classes equal value:

| Prototype work package | Weight | Assessed completion | Weighted contribution |
| --- | ---: | ---: | ---: |
| Prop/assembly foundation | 15% | 90% | 13.50 |
| Bottle dynamics/handling | 15% | 85% | 12.75 |
| Current manual manufacturing | 15% | 85% | 12.75 |
| Shelf/NPC/service integration | 15% | 85% | 12.75 |
| Checkout/economy | 15% | 95% | 14.25 |
| Persistence | 10% | 85% | 8.50 |
| Frontend/network integration | 10% | 80% | 8.00 |
| Final package/hardware acceptance | 5% | 30% | 1.50 |
| **Total** | **100%** | | **84.00%** |

The full-game illustrative weighting is foundations15% at85%, production/equipment20% at30%, hazards/material depth15% at10%, patron systems15% at20%, progression10% at0%, logistics10% at5%, platform delivery5% at45%, final content/presentation10% at15%: **27.5%, rounded28%**. Large uncertainty reflects undefined content and platform scope. The narrow Normal prototype is much farther along than this broad game.

The remaining16% of the current slice is disproportionately integration/acceptance work. It should not be read as “only16% of calendar time remains.” Reproduced player-facing failures could move the estimate downward.

## Prioritized prototype roadmap and effort

Effort estimates are person-days for an experienced Unreal developer with QA help, existing assets and decisions available. They are planning ranges, not measured tickets or promises. Parallel work overlaps; do not add every range as a guaranteed calendar schedule.

1. **Freeze and reconcile the candidate —1–2 days.** Name the manual-filling specification as current; decide whether cooling/free hose spray are mandatory for this delivery. Review modified/untracked assets and source, capture the current plugin and soft-reference cook rules, keep credentials excluded, and retain rollback/save fixtures. Exit: one reproducible source/content revision and explicit acceptance checklist.
2. **Package the exact current game —1–3 days.** Build clean Editor and Development game targets, cook only intended maps/dependencies, check current shell/liquid/cap meshes and menu, launch offline and Epic configurations without Editor. Exit: two identical standalone candidates with all runtime assets present.
3. **Run a human solo and two-player loop —2–4 days including triage.** Empty bottle → source connection/alignment → hold/release fill → vent or temporary seal → attach/twist cap → shelf → NPC bucket/queue → scan/save/lock → tender/change → complete → ATM → buy/produce again. Exercise G/GG, F/R/E, focus loss, Escape, disconnect and teammate handoff. Start from both a clean slot and a copied existing save.
4. **Fix observed physics/content failures —3–7 days allowance.** Address warning sources, below-world live stock, saved cap poses, held/dropped shake/launch/empty settlement, collision and rendering failures as reproduced. Exit: affected regressions and rendered remote acceptance pass; no known loss/duplication blocker.
5. **Run packaged lifecycle/economy soak —2–4 days.** Two and four players, repeated sales/ATM/purchases, concurrent machines, late join/rejoin, packet impairment, save/restart and receipt/debt recovery. Measure FPS/frame time, save latency, memory and bandwidth. Include empty-source/out-of-money/blocked-route recovery.
6. **If full written GDD prototype workflow is required, add it —5–15 additional days, design-dependent.** Implement practical cooling and physical hot-source/hose/nozzle/uncaptured-spray behavior with existing liquid identity/accounting, then repeat integration acceptance. Do not rebuild the transfer or wallet architecture.
7. **Release qualification —1–3 days after fixes.** Verify package provenance, no accidental test funding or QA modules, clear controls, stable saves, startup/quit and known-limit notes. Test a different PC/network for online delivery; verify voice if promised.

**Planning allowance:** roughly **2–4 developer-weeks** for a stable internal/manual-filling prototype candidate, plus dedicated playtesting; **4–7 weeks** if the broader hot-source/hose/cooling requirements must also ship. These ranges assume no major new physics/network redesign and exclude storefront review waiting time. The current build can be playtested before this work is finished. Online service acceptance is a separate gate from a repeatable local production loop.

## Full-game milestones

| Milestone | Main deliverable | Rough additional effort |
| --- | --- | --- |
| M1: accepted Normal slice | Above prototype gates, reproducible package and saves | Included above |
| M2: production/logistics vertical slice | Cooling/hose handling, carts/forklift, energy and one advanced machine; robust transport/network integration | 8–16 person-weeks |
| M3: vessel and hazard vertical slice | One complete non-Normal hazard with countermeasure, material/closure progression and washing/reuse | 8–16 person-weeks |
| M4: complete game rules | Patron differentiation/anger, shifts, quotas, ratings, rent/loss, upgrades and balance | 8–14 person-weeks |
| M5: content production | Remaining cola/equipment/patron variants, authored levels, final machinery art, VFX/audio and onboarding | 20–40+ person-weeks |
| M6: platform and release | Steam/Epic matrix, voice, save migration, optimization, accessibility and extended multiplayer QA | 10–20+ person-weeks |

These work packages overlap and carry substantial uncertainty. A broad full-game allowance is **approximately60–110+ person-weeks**, before a firm console/certification plan. A small multidisciplinary team may overlap engineering, content and QA; a solo calendar forecast would be much longer. Do not derive schedule by dividing code completion by elapsed development time.

The efficient route is to preserve the existing identity/state/economy architecture, qualify one complete player-driven Normal loop, and add vertical slices through those services. Expanding supernatural content or replacing working systems before that acceptance would increase integration risk.
