> Current update: see [Multiplayer game integration](Multiplayer_Game_Integration_2026-11-14.md) for the installed runtime patch, native/IP/regression reruns, Epic service evidence and remaining acceptance. The original baseline report below is historical.

# Universal Crossplay implementation report

Recorded 14 November 2026. Version 0.1.0 is a development plugin for UE 5.8.2 Windows x64 and two-player listen-server play. Its independent implementation and local validation are complete. Production storefront acceptance is pending configuration and real accounts; this report does not classify those services as tested or release ready.

## Implemented functionality

| System | Implementation | Validation and remaining work |
| --- | --- | --- |
| Plugin packaging | Separate descriptor, early SDK bootstrap and runtime module; no game classes or assets | Standalone Editor, Development and Shipping compilation; final package logs retained |
| EOS SDK | Project-local supplied 1.19.2.1 runtime selected before EOSShared starts | Direct SDK and engine-manager version checks pass; no engine files replaced |
| Authentication and identity | Steam Connect-only flow; Epic EAS plus Connect; explicit portal/developer login; common EOS PUID; logout and status handling | Compiles; real account login, linking/consent and refresh tests pending |
| Lobbies and sessions | Public/private create, discovery, compatibility filter, ID/invite join, owner update, ready/start, listen travel, leave, last-lobby reconnect | Native authority tests and independent RPC test pass; external service and full online travel acceptance pending |
| Player management | Small replicated member list, authoritative owner/ready state, departure cleanup, host kick; EOS peer/PUID association check | Local membership and permission tests pass; actual EOS transport association and kick API acceptance pending |
| Friends and invitations | Distinct Steam/Epic friend sources, Steam rich-presence/overlay callbacks, EOS invitations, explicit PUID invitation | Compiles; real overlays/invitations pending. Private Steam invites require an existing EOS mapping |
| EOS networking | Engine NetDriverEOS and SocketSubsystemEOS; EOS resolves peer address; existing engine replication retained | Adapter compiled; independent IP host/client pass. EOS NAT, relay and different-network P2P are not tested |
| Voice | OSS-owned EOS RTC user and lobby channels; default mute, device selection, volume and per-player mute | Compiles; actual channel admission, microphone permissions and audible audio require account tests |
| Text chat | Owned server RPC, bounded wire payload, server-supplied sender, sequence/rate validation, small multicast delivery, local history capped at 50 | Native validation and real local host/client delivery pass. No bulk history or chat persistence |
| Failure handling | Operation deadlines, weak callbacks, delegate cleanup, travel/network failure handling and explicit leave/retry | Core code compiles; real service interruption, expiry and soak testing pending |
| Progression adapters | Whitelisted achievement writes per provider and EOS statistics updates | Disabled until IDs configured; service calls not externally tested |
| Example integration | Optional native menu callable from Blueprint/C++; no required gameplay reparenting | Menu rendered and inspected at 1280 by 720; online actions pending configuration |
| Configuration helpers | Ignored private store profiles, SDK runtime mirror, optional official desktop bootstrapper generation, offline validation project/runner | Profile tests pass 4 of 4; bootstrapper generation passes without service installation or publishing |

## Architecture and files

`UniversalCrossplay.uplugin` declares the two modules and dependencies on Unreal's EOS, Steam, socket and voice plugins. `UniversalCrossplayBootstrap` selects the runtime through the engine-supported selector at EarliestPossible. EOSShared remains the sole owner of SDK initialization, platform ticking and shutdown.

The runtime exposes `UCrossplaySubsystem`, a GameInstance subsystem with Blueprint functions and events. Provider lifecycle state is private. Authentication, sessions, social, Steam bridging and voice have separate implementation files. It uses OSS v1 consistently; it does not combine EOSPlus and the Online Services v2 API.

`UCrossplayPlayerComponent` is an optional owned PlayerController RPC endpoint. `ACrossplayLobbyState` contains small authoritative lobby data. `UCrossplayWorldSubsystem` attaches these for an active online world, or when explicitly enabled for local testing. Gameplay inventory, bottles, currency, customers and save data stay in the game. The clock used for server rate limits is monotonic so a persistent controller does not inherit invalid world-time budgets across seamless travel.

`UCrossplaySampleWidget` and `UCrossplaySampleUI` provide a replaceable native example. `CrossplayTypes` contains the Blueprint data structures and bounded chat serializer. `CrossplaySettings` contains project configuration with online disabled by default. `Scripts`, `ConfigExamples` and `Docs` contain installation, configuration, API and validation material. ThirdParty records the supplied SDK and notices.

The public UE bridge is compiled against the engine's bundled 1.19.1.2 headers; stable bridge calls resolve to the selected supplied 1.19.2.1 runtime. Independent runtime checks prove binary selection, rather than assuming a copied DLL was used. Full platform-service compatibility still requires the account tests below.

## Test evidence

- Standalone native report Native12: 4 passed, 0 warning tests, 0 failed.
- Final game-integrated plugin report GamePluginNative13: 4 passed, 0 warning tests, 0 failed.
- InstalledNative14: 4 passed, 0 warning tests, 0 failed. The live installation loads the unchanged game module 14120 and EOS 1.19.2.1. Test user data is isolated from the game's actual saves.
- Final independent IP report Network13: all 5 checks passed, covering server acceptance, client observation, selected SDK, absence of fatal errors and clean exits. Chat, both ready members and non-owner start rejection run across actual processes. Requested network emulation is 50 ms and 2 percent loss; this is not a measured network-quality or EOS P2P result.
- Existing game regression report GameRegression12: 66 passed, including 60 clean and 6 warning tests; 0 failed or skipped. The warning test paths match BottleLifecycleFull78, the preceding baseline. Warnings concern physics fixture collision settings, the legacy migration fixture and ABP_Unarmed's thread-safety warning; they have not been relabeled clean.
- Standalone UAT BuildPlugin compiles the plugin in a generated host with no game module or assets. Editor, Win64 Development and Shipping builds are checked separately.
- Sample menu screenshot inspected. Private-profile generation passes 4 checks with disposable fake values. Official desktop bootstrapper/config generation passes without installing or launching it.

Earlier packaging attempts exposed missing Blueprint categories and an implicit weak-pointer header dependency, both corrected. A long Windows output path was shortened. When the drive filled, obsolete task-generated output and a reproducible harness build cache were removed; the successful build was then rerun. No live save data was deleted or reset.

## Current game integration

The validated package is installed under the game's `Plugins/UniversalCrossplay`. The only game configuration changes are enabling that descriptor, selecting a project-owned EOS binary for staging, and explicitly retaining `bEnableOnline=false` until service setup is complete. No game C++ class, Blueprint, material, map or save format is changed by this integration. The existing native game module 14120 is preserved.

The game's configuration, module manifest and all current save files were backed up after the editor was saved and closed. Installation.json verifies the game source, module manifest and all backed-up saves remain unchanged. Test runs use separate user directories. The backup is `Saved/CrossplayIntegration/20261114_173939` inside the game project; it contains BeforeIntegration.zip, Baseline.json and Installation.json.

## Configuration needed from the project owner

1. Create/select the Epic product, development sandbox and deployment. Record their IDs and the artifact name.
2. Create appropriate development client policies and clients for Connect, lobbies and lobby voice. Link the Epic client to an EAS application with the required scopes and test-user access.
3. Obtain the actual Steam App ID and test-account licenses. Configure Steam as the product's EOS identity provider, with matching Web API ticket settings. Enter any publisher key only in the trusted portal configuration.
4. Store game-client configuration in a private local JSON and generate ignored Steam/Epic profiles. Do not send credentials through chat or commit them.
5. Choose cooked lobby, gameplay and return maps and a shared compatibility ID. The plugin does not select or modify game maps automatically.
6. Provide two authorized accounts per store and preferably two machines on different networks for acceptance. Review desktop crossplay bootstrapper/service prerequisites before store publication.

`SETUP.md` gives the detailed sequence and official references. No developer-account settings, live services, store publishing, billing or OS service installation have been changed. Account setup is the current dependency for real crossplay validation.

## Remaining acceptance and limits

Steam to Steam, Steam to Epic, Epic to Steam and Epic to Epic must all pass real login, public/private visibility, invitations, join, ready/start, lobby-to-game travel, chat, audible RTC, disconnect/reconnect and leave/recreate checks. Add expired/invalid credentials, lost connectivity, full/incompatible lobbies, stale invites and extended runs. Test different public networks and packaged game builds, not just editor clients.

This is a Beta development implementation, not a production-certified crossplay release. Shipping plugin compilation does not establish a cooked game's service behavior. Only UE 5.8.2 Windows x64 has been validated. The sample menu is functional scaffolding, not final game UI. Provider friend lists remain distinct. Private Steam invitations require the recipient to have signed into the deployment previously. Reconnect requires the original listen host to remain available. Host migration is not included.

Dedicated servers remain future work: server target/build, hosting allocation, server authentication and admission, trusted configuration, map deployment and remote-only tests. Global service code, owned RPC components and gameplay authority are separated so that work does not require rewriting the game's replication. The current host/start workflow is intentionally listen-only.
