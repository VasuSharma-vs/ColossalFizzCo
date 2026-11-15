# Modular bottles, contents, caps and product identities

Designer-approved specification, 15 November 2026. This supersedes earlier flavor-owned capacity and empty-to-filled bottle SKU replacement rules. Implementation acceptance is recorded separately; a design rule is not evidence of a test pass.

## Shared definitions and identities

The central `DA_ProductCatalog` is an identity registry. It stores stable automatically assigned PIDs, readable names, categories and references to definitions. It references `DB_Bottles`, `DB_Contents` and `DB_Caps`; these typed databases reference the corresponding shared assets. Gameplay settings live in those definitions rather than being duplicated in catalog rows.

New PIDs are allocated without reusing recorded identities. Renaming or reordering entries preserves their PIDs. The editor provides searchable named product selections filtered by category; a cap's supported bottles offers bottle entries. Missing definitions, duplicate IDs, invalid categories, missing geometry and invalid bottle references must be reported by validation.

Bottle definitions own physical meshes, mass, pressure settings and maximum portions. The runtime bottle record owns its current amount. Content definitions own liquid properties and a supported-bottle visual recipe for each Bottle PID: liquid mesh, active skeletal mesh, materials, effects and attachment transform. Contents have no bottle capacity/current-portion setting. Cap definitions own their geometry, installation settings and supported Bottle PIDs.

Current prototype identities are Plastic Bottle1000, Normal Manual Twist Cap1001, and Normal Cola contents901–905 (Lemonade, Mint, Sweet, Spicy, Zero Calories). These are actual allocated IDs, not an instruction to enter integers manually. Configure the bottle's maximum in `/Game/Props/Products/DA_Bottle_Plastic`. The old flavor assets are initial-stock presets; their old portion field no longer determines the Normal bottle's capacity.

## Instance state and assembly

The existing Prop Manager remains the authority. Every bottle, content child and cap has a distinct ISID and its own shared PID. The existing `Attachment.OwnerISID` is the immediate parent; zero means no parent. `ChildISIDs` contains direct children only and is rebuilt from owner edges. These relationships are unrelated to network actor ownership.

Current portions are derived precisely from the bottle's canonical content level and maximum portions. Existing temperature, pressure, damage, cleanliness, creation time and liquid creation time remain canonical per-instance data. Shared assets never contain instance owner/child IDs.

Editing a shared bottle capacity preserves existing liquid amounts. For example, a saved bottle containing2 portions out of4 becomes25% full when its maximum changes to8; it still contains2 portions. New placed stock uses its configured initial fill percentage. Save/load derives the original amount from the saved definition and instance state before applying the current capacity. Existing clients apply the same conversion when the shared definition updates, without adding another quantity stream.

Filling validates the selected Content PID against the bottle's canonical PID before changing state. A successful first fill creates one content child. Further filling of the same content updates that child's amount without replacing the bottle PID or ISID. Filling preserves the bottle's physical history and starts only the new liquid lifecycle. Unsupported fills change neither contents nor child ownership and show **Content not supported** in red at the top right.

A cap's supported bottle list is checked before installation and manual rotation. Attaching or removing it updates both immediate relationship sides and the existing cap/bottle state links. The same cap ISID is retained. Unsupported installation shows **Cap not supported** in red at the top right.

## Representation, persistence and multiplayer

One content PID may have several bottle-specific mesh batches. A visual-bottle selector identifies the appropriate batch; it is not another content product identity. Active assemblies use components on one root ASM; resting components use their own persistent ISM slots. Promotion parks the existing slots; demotion restores them. Neither operation creates replacement ISIDs. Interaction with a cap or descendant resolves and activates the root assembly.

Save schema6 preserves exact relative and world transforms. Historical cap-mount correction applies only to older schemas. Saved pressure, fill, temperature, damage, seal state and identity survive transitions and loading.

Shared recipes use the existing bounded definition stream: at most896 definition bytes per chunk, two instance records per message, two messages per peer per manager tick and eight unacknowledged messages. Inactive objects do not acquire per-frame asset or hierarchy updates. Direct children and current portions are reconstructed from existing authoritative data instead of being replicated as duplicate arrays/amount streams.

## Compatibility with historical data

Existing saves used world-local product IDs whose meanings differ across save slots. Those numeric IDs cannot be globally reassigned by guessing. Old IDs and records are retained; authoring asset identity and explicit catalog references resolve canonical products. New empty-bottle and cap offers select canonical products. Old filled presets retain their initial-content data and saved identities. Older material-based factory-cap entitlement is converted into an explicit runtime legacy whitelist, preserving already-approved behavior while new typed definitions use strict bottle-PID compatibility.

The demo remains limited to plastic bottles, the five Normal Colas, and normal manual twist caps. Current player, NPC and bottle sizes and mesh pivots remain unchanged. Raw ingredients, other materials, hazardous cola effects and incompatible-fill failure events remain outside the demo.

## Acceptance requirements

Verify empty pickup, compatible filling and portion conservation, one content child after repeated filling, compatible/manual capping, rejected combinations without mutation, cap removal, exact ASM/ISM round trips, child-hit root activation, save/load and host/client/late-join synchronization. Test results and limitations belong in the dated implementation report.
