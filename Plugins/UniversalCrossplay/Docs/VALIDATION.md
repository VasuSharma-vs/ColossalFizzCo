# Validation and troubleshooting

## Current Epic Editor acceptance — 14 November 2026

The installed runtime patch passes the final real two-account Epic run: 29 host and 25 client assertions. The isolated game's 2,384 source/content files were hash-matched to the current game, and its tested runtime DLL matches the installed DLL. Verified: EAS/Connect identity, Epic friends, public create/search/join/update, hosted-search phase preservation, EOS transport, authoritative chat/ready/start, prototype-map travel, actual character movement replication, leave/reconnect, private visibility/invitation/join/chat, owner kick and cleanup/logout. RTC room connections and mute/volume API calls pass; audible voice remains unaccepted.

Native22 and InstalledNative24 each pass 4/4 clean; installed IP Network25 passes 5/5; game regression27 passes 66/66 with the same six warning-test paths as its baseline. These tests use NullRHI and isolated user directories. No game source, assets, saves, size settings or player movement code were changed by the integration. The original polling-ticker movement fixture was corrected to supply input through a test-only world-tick component before CharacterMovement; its earlier failures are not treated as game replication defects.

The current runtime patch was rebuilt for Editor. Earlier standalone Development/Shipping packaging is historical evidence for 0.1.0; refresh those configurations before distributing this patch. Rendered gameplay interactions, a freshly cooked online game, audible RTC, different public networks, expiry/interruption/soak tests and all Steam combinations still require acceptance. Read-only query timeout preservation is implemented but not fault-injection tested.

## Completed independent validation

- The plugin compiles in a minimal Harness project with no ColossalFizzCo module or assets.
- Standalone UAT BuildPlugin passes Editor, Win64 Development and Win64 Shipping. The generated host project has no game module or content. Stricter packaging exposed missing Blueprint property categories and a missing explicit weak-pointer header; both are fixed. A Windows path-length failure was resolved using a short output directory. A full-drive failure was resolved before the successful retry.
- Final Native12:4/4 pass,0 warnings. Tests cover 256-character wire round trips, text validation, monotonic sequences, rate limits, two-player membership, owner-only ready/start rules, departure/rejoin, and EOS runtime version.
- Final real IP host/client run Network13:5/5 pass after the monotonic-clock correction for travel-safe chat rate limiting. Both peers used EOS1.19.2.1 through the engine manager; owned chat RPCs, two ready members, rejected non-owner start and clean exits passed. Requested emulation was50 ms/2% loss. This was an independent IP test, not EOS P2P or storefront acceptance.
- The optional native sample menu was rendered and visually inspected at1280x720 in the empty project. Its offline state, host/search/invite/ready/start/chat/mute controls rendered. This smoke check does not prove successful online button actions or audio.
- Private-profile generation tests:4/4 pass, using disposable fake configuration. Official desktop bootstrapper/config generation passed without launching the game, installing the service or publishing anything.
- Game integration: the plugin compiles alongside the existing game without game-source changes. Its native tests pass4/4 there (GamePluginNative13). The existing game suite passes66/66 (GameRegression12):60 clean,6 warning tests,0 failed/not-run. The same six warning test paths occur in the preceding baseline. Full online map travel and rendered/audible storefront behavior remain pending.
- Installed-copy acceptance: InstalledNative14 passes4/4,0 warning tests. Its log confirms both the preserved game module14120 and EOS1.19.2.1 were loaded. The installation manifest verifies unchanged game source, game module manifest and backed-up saves. Online mode remains disabled until service configuration.
- Epic-first setup update: profile-generator validation passes10/10 named scenarios covering the original paired mode, Epic-only mode without Steam credentials, Steam-only mode without Epic credentials, correct provider settings and validation failures without profile mutation. This changes setup scripts/templates/documentation only; native sources and binaries remain unchanged. It does not establish service login or lobby acceptance.
- Later build/test results are recorded in the implementation report and machine-readable checkpoint. Do not infer service acceptance from compilation.

## Required platform matrix

Steam combinations remain pending. Epic-to-Epic Editor functions passed above; its remaining platform acceptance is listed separately:

| Host | Joining player | Required checks |
| --- | --- | --- |
| Steam | Steam | Connect-only auth, public/private host/search/join, Steam invitations/overlay, RTC/chat, travel and gameplay |
| Steam | Epic | Common deployment discovery, private invites where supported, RTC/chat, travel and gameplay |
| Epic | Steam | Same checks with Epic hosting; Steam must not be forced into an Epic account portal |
| Epic | Epic | Editor facade, friends/private invitations, chat, ready/start, map travel, character movement and lifecycle pass; audible RTC, rendered/cooked gameplay and different-network acceptance pending |

Use two machines/accounts where possible. Verify different public networks for NAT/relay acceptance. Use unique test save directories. Run leave/recreate cycles, owner departure, client drop/reconnect, invalid credentials, token refresh/expiration, a full lobby rejection, private lobby hidden from public discovery, stale invitations, incompatible CompatibilityId and malicious/replayed RPCs. Confirm microphone mute, per-player mute, volume and device changes on both storefronts. Check the actual rendered and audible results; event callbacks alone do not prove voice quality.

## Build and run

Build the host project's Editor and Development game targets. Run the native filter UniversalCrossplay with a disposable UserDir. For independent local network tests use the provided harness runner; CrossplayNetworkTest is a development-only fixture. It does not run in normal play or shipping and does not grant online credentials.

`Scripts/CreateValidationProject.py --output <empty-folder>` creates a minimal offline C++ host referencing this plugin, without game assets or credentials. Build its CrossplayHarnessEditor target with UE 5.8. Then run `Scripts/RunLocalIPValidation.ps1 -ProjectFile <CrossplayHarness.uproject> -EditorExecutable <UnrealEditor.exe> -OutputDirectory <new-report-folder>`. The runner preserves logs and a Result.json, uses isolated user directories, and exits on a bounded deadline. Use the offline harness for this test, rather than a profile configured for real online services. Keep the generated project in a short path on Windows.

For online tests generate private profiles, restart with the chosen development CustomConfig, sign in through the actual storefront or Epic developer flow, and use the public plugin API. Wait for OnOperation completion. Host Lobby -> join with a second account -> ready both -> start -> text/voice -> disconnect -> leave -> recreate. Read diagnostics and both logs. Never label the four storefront combinations passed solely from LAN or mocked identities.

## Common errors

- SDK mismatch: ensure Bootstrap loads at EarliestPossible, its DLL is staged, and restart the editor. The startup log must show Initializing EOSSDK Version:1.19.2.1. Other plugins selecting a different runtime are an integration conflict; do not initialize a second SDK.
- Online disabled/missing artifact: complete the setup guide and local private profile. The default intentionally keeps existing IP gameplay available.
- Steam not initialized: use the right profile, Steam client/account, application license and App ID. Verify the configured Web API ticket identity matches Epic's provider settings. No publisher key belongs in the client.
- Epic access/consent failure: verify the application/client link and the development account's access to the product. Account linking and brand/access requirements differ from Connect-only game-service identity.
- Lobby not found: check product/sandbox/deployment, CompatibilityId, permissions, public/private flag and capacity. Steam and Epic builds must share the same EOS deployment.
- Private Steam invitation rejected: the Steam friend needs an EOS product mapping from a previous sign-in. The plugin sends a real EOS private invitation before the native Steam join request; it does not treat an arbitrary join URI as private access permission.
- Identity mismatch on connection: the online PlayerState PUID must match its EOS transport peer. Verify DefaultPlatformService=EOS and the EOS driver. This association requires real-service acceptance.
- Friends missing: Epic EAS and Steam friends are separate services. A Connect-only Steam user does not automatically obtain an Epic friend list.
- No voice: enable deployment/lobby voice, unmute deliberately, verify microphone/device privacy and use two accounts. The plugin uses the OSS-owned RTC user; do not release or independently initialize it.
- Map travel failure: configure actual cooked LobbyMap/GameplayMap/ReturnMap paths. Preserve normal Super calls in custom GameMode admission logic.
- Login/operation timeout: requests are bounded. Retry after connectivity/account checks; interrupted sessions must finish cleanup before another host/join.

## Remaining integration acceptance

Current source exposes the required provider calls and reusable components. Epic Editor authentication/discovery/P2P, private service invitations and lifecycle now pass. Steam, different-network NAT/relay, overlays, audible voice, achievements and stats remain unverified externally. A production release still needs those tests, application/brand/store approvals where applicable, and performance/soak acceptance. No dedicated server deployment is included.
