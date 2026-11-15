# Modular plastic bottle assemblies

**Current bottle prototype checkpoint 14 November 2026:** Read [the saved progress and questions document](Bottle_Prototype_Progress_and_Questions_2026-11-14.docx) for the installed build, verified tests and remaining acceptance work. The current repair uses artist-aligned cap pivots, paired shell and liquid morphs, compact continuous pressure updates and Rocket Projectile Component flight. Keep the dated entries below as historical evidence.

## Prototype behavior

The Normal Cola supply catalog configures the plastic shell, liquid interior, and manual twist cap. Each part has a product ID, an individual persistent ISID, and a reserved ISM slot. The bottle owns its content level and pressure. An attached liquid has no collision or independent physics. The active shell supplies the physics body, and its installed cap contributes its configured mass.

The Prop Manager resolves an interaction on a child to its root. Active children are local mesh components of the root actor. The liquid identity component owns a skeletal visual while active so its OverfullLevel morph follows the shell. Its static visual remains hidden until ISM restoration. When the root settles, the manager captures each component’s actual transform, including a cap’s twist, and restores the original slots. A removed cap keeps its identity and can simulate independently. The saved immediate parent and local transform reconstruct the child index on load. The manager rejects missing parents, cycles and trees deeper than 64 nodes.

## Authoring

Open `/Game/NormalColaPrototype/DA_NormalColaSupplyCatalog` and expand **Assembly**. Bottle rows select the shell and its mounts. Content rows select permitted bottle materials, optional cap restrictions, and a mesh/material per bottle material. Cap rows reference the existing cap product assets and provide the cap mesh, permitted bottle materials, mass and mount offset. The updated static and skeletal plastic shells each use the single authored material slot `Cola_001`. The inner mesh owns the flavor material in slot zero in both representations. The cap uses the supplied SM_NormalPlasticCap with the same authored origin as the bottle. Its mount is identity, with no hardcoded neck translation. Delivery trays use mesh bounds to place the visible cap geometry correctly. Deprecated serialized prototype cap recipes are migrated to this supplied mesh and mount while preserving instance identities. The existing product and content definitions supply prices, portions, density, pressure, expiry and physics settings. The prototype includes the five Normal Cola flavors and the manual twist cap. New gameplay for other cap mechanisms or hazardous colas requires separate implementation.

## Replication

Each remote controller receives an owner-only prop delivery actor. It serializes shared definitions once per registration or snapshot and splits their serialized bytes into payloads of up to 896 bytes. A records message carries at most two instance records and at most 32 removals of each kind. Each manager tick can issue at most two messages per peer, with at most eight unacknowledged. Clients confirm sequence numbers. A new peer receives its own initial snapshot. Subsequent changes to already-sent IDs are coalesced and follow the snapshot. Active transforms and compact gameplay events also use existing separate update channels. The 896-byte limit applies to definition data, not aggregate traffic or complete transport packets.

The server persists one hierarchy edge per child. Clients reconstruct the inverse child list and component tree locally. Mesh geometry and inactive child transforms are not streamed per frame. Repeated identical placements send a product ID, count and base transform. Large batches of client promotion requests are spread across updates.

## Earlier assembly integration verification on 14 November 2026

The editor reported the same single material slot, Cola_001, on both SM_PlasticBottle and SKM_PlasticBottle. The static shell extends from Z 0.648 to 186.589 cm at native scale. The shell uses its authored material, and the separate inner mesh supplies the liquid material in both representations. The regression test checks this on active components and after returning to ISM. The provided shell and inner mesh packages are source art. An independently centered loose-cap derivative was created in the earlier integration. The current repair no longer selects that derivative. Both supplied source meshes remain unchanged.

The isolated UE 5.8 Development Editor target built successfully. Native automation ran all 64 ColossalFizzCo tests, with 59 clean passes and five passes with warnings. Four reported a collision/simulation configuration warning and one reported the existing ABP_Unarmed animation thread-safety warning. The tests cover saved identities, nested transforms, incompatible caps, cap mounting inside an active volume, rocket discharge after cap ejection, and a snapshot with over 1,024 distinct instance IDs.

The network manufacturing fixture used separate listen-server, player, and late-joining client processes on the prototype validation level, with 75 ms configured packet lag and 2% packet loss. All three observed the shelved Mint bottle with shell ISID 11112, liquid ISID 11113, cap ISID 11114, full fill, and a closed seal. The source cylinder retained 90%. The server registered completion of the two players’ initial snapshots at sequences 629 and 637. The final shelf transfer preserved bottle strength at 100%. A preceding run reproduced damage during filling when normal settling re-enabled physics in the holder. The holder now binds the current bottle instance, releases it for pickup or launch, and ignores its own support contact.

Reproduce the native suite with the automation filter ColossalFizzCo. Run Scripts/TestColaAssemblyLine.ps1 with -ManufacturingOnly -PacketLagMs 75 -PacketLossPercent 2 -LateJoin for the network scenario. The fixture uses isolated funds and a real remote client. Player-input feel, GPU appearance, packaged-build behavior and measured bandwidth in bytes per second require separate validation.

## Cooking runtime-selected meshes

The native catalog can select the plastic inner meshes at runtime. Config/DefaultGame.ini explicitly includes /Game/Assest/meshs/PlasticBottle in DirectoriesToAlwaysCook. Keep that cook rule when packaging; editor-only availability does not guarantee a soft-referenced mesh is in a cooked build. Candidate14118 container inspection verifies both SM_PlasticBottelInnerContent and SKM_PlasticBottelInnerContent, the shell and the supplied shared-pivot cap.

The prototype customer visit replicates its bucket reference and two small original-collision enums. Carried buckets are query-only and ignore pawns; docking/home restore collision. Queue spacing accounts for carried bounds and checkout ownership remains reserved until the departing container clears the dock.
