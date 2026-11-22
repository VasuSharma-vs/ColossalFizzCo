# BP_ColaHose trace and grab repair — 18 November 2026

Asset: `/Game/Assest/Blueprints/Cola_Container/BP_ColaHose`.

This Blueprint is a preset of native `AHoseCableActor`. Its Blueprint event/construction graphs contain no custom hose logic. The native actor owns a `CollisionCableComponent`, a finite-buffer `HoseTransferComponent`, and two connector meshes that follow the first and last simulated cable points. The inspected preset has 1,200 cm cable length, a 500 cm authored end offset, 32 segments, and grabbing enabled.

## Causes

1. Both inherited cylinder connector meshes were explicitly set to **NoCollision**, so the player's Visibility trace could not hit them.
2. The custom cable is a rendered `UMeshComponent` without a query body matching its deformed tube. Setting its collision mode to QueryOnly does not itself create that body. The player's generic `LineTraceByChannel` consequently misses the tube.
3. The existing G gesture adapter emitted ordinary prop grab/place/drop intents. Its Blueprint continuation required a traceable, interface-grabbable prop/component, while this hose uses the native cable-point grab system.
4. The native server grab validation searched for a nearby raw simulation knot. A valid hit in the middle of a long span or on a rounded visual bend could be rejected even when the cable was visible.

Ignoring Pawn collision is separate from blocking Visibility traces. Physical player collision remains disabled for the hose interaction geometry.

## Repair

- Connector meshes use **QueryOnly**, block **Visibility**, and ignore other channels, including Pawn and the cable's world-simulation queries. Construction and BeginPlay also apply this to saved presets that serialized the old NoCollision setting. Connector art and offsets are retained.
- Native G/E cable selection traces analytic capsules along the **published visible curve**, including its width and configured aiming tolerance. A connector mesh hit explicitly selects its corresponding endpoint, including connectors offset outside the tube's pick radius.
- The server validates hits against the visible spans and maps them to real simulation points. Reach, ownership, grabbing switches and visibility checks remain enforced. It checks the actual selected point as well as the padded hit, preventing aim tolerance from reaching through a wall.
- Holding **G** routes an empty-hand cable pickup through the existing owned `ShopPlayerInteractionComponent` and its server grab path. **Double-tap G** releases; holding G while already holding a cable also releases it without teleport placement. The same press that picked up a cable cannot immediately place/release it. Ordinary prop gestures continue through their existing Blueprint path.
- The socket connection trace ignores the currently held hose's connector art, so making the connectors traceable does not hide the destination socket.

The cable picker is arithmetic on cached render geometry; it adds no world collision queries, physics mesh cooking or per-point query bodies. The generic world line trace still has no body for the rendered tube: G/E use the dedicated cable picker, while connector meshes support ordinary Visibility traces. This update does not add a generic world-query body or change the player's legacy per-tick trace/highlight Blueprint graph.

The player Blueprint had **unsaved edits** in the running editor. No Blueprint or map was rewritten or saved, and its existing G callbacks invoke the repaired native component. The open Unreal editor was not closed or restarted.

## Validation

Development Editor build **18122** succeeded. An isolated headless editor loaded byte-identical copies of the built runtime/editor DLLs and the real saved content. The selected hose, cable, grab gesture, authored bottle grab/release and authored player input suites passed **73/73 tests**: 70 clean, 3 warning-bearing, zero failures/skips.

Added coverage:

- Spawn the actual saved `BP_ColaHose`; ordinary complex Visibility traces hit each correct connector mesh while Pawn/world-simulation responses remain ignored.
- Pick the middle of a coarse span, tube caps, padded picks and a rounded cubic bend; map hits to simulation points; reject out-of-tube hits and disabled grabbing.
- Native local-player view selection grabs both tube spans and offset connector art, and rejects foreground-wall and padded-through-wall attempts.
- A held traceable connector does not obstruct the resource-port connection action.
- Dispatch the **real compiled IA_Grab callbacks** on the saved `BP_FirstPersonCharacter` with a windowless local viewport. Holding G grabs the actual saved hose, does not attach the actor through the legacy carry state, and double-tap G removes the authoritative grab.

The initial run's two new view fixtures lacked a local player and failed selection before reaching the grab path. Adding a local player fixed the fixtures; the final full selection passes. Initial evidence is retained. A pre-existing unity-build name collision also required renaming two dashboard-local color variables (`Cream`/`Muted`); dashboard behavior and appearance are unchanged.

Evidence: `Saved/ColaHoseGrabRepair_20261118/Build.log`, `FinalReport/index.json`, `FinalTests.engine.log`, `BinaryProof.json`, `AssetsBefore.json` and `VerifiedSummary.json`. These are native/windowless input tests, not a rendered manual playthrough or a real remote-client network session.
