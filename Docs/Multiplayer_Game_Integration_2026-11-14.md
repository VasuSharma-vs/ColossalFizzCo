# Colossal Fizz Co — Multiplayer game integration

Updated 14 November 2026. This records the installed development integration, independently from storefront acceptance. No UI redesign or replacement pawn movement was added.

## Installed in the game

- `Plugins/UniversalCrossplay` remains the reusable service layer. No game-specific parent classes or assets were added to it.
- The current Editor runtime includes the hosted-search state fix: finding lobbies while hosting/playing restores the original phase when the query finishes. A search/friends timeout preserves the unrelated active session. The timeout branches still need fault-injection acceptance.
- Development builds expose `Crossplay.Run` to call the same public API without building a menu. Its requests retain the existing authority checks and are excluded from Shipping. See the plugin's `Docs/API.md` for the command sequence.
- The private `Config/Custom/Epic/DefaultEngine.ini` is configured for the actual `/Game/NormalColaPrototype/Lvl_NormalColaPrototype` gameplay map. `/Engine/Maps/Entry` is the temporary development lobby/return map. `DefaultGame.ini` in the same profile includes these maps for cooking; a newly cooked online game package has not been accepted yet.
- Launch with `-CustomConfig=Epic` to select this configuration. The ordinary project configuration remains offline. The Epic client secret and encryption key remain in ignored local files; Git ignore checks passed. Steam is disabled in the Epic profile.
- Game native module 14120, game source and actual save files are preserved by the deployment baseline hashes. No gameplay assets, bottle sizes, character sizes, save formats or economy rules were changed by this patch.

## Verified against the installed copy

| Evidence | Result | What it establishes |
| --- | --- | --- |
| BuildApiChecks22 | Passed | Updated Editor runtime and separate development QA module compile alongside the game |
| Native22 | 4 clean passes | Bounded chat payload, replay/spam rules, owner/ready/departure rules and selected EOS SDK |
| InstalledNative24 | 4 clean passes | The installed plugin loads with game module 14120 and EOS SDK 1.19.2.1; the development console command is registered |
| InstalledNetwork25 | 5/5 checks passed | Real local IP host/client chat, ready state, owner permissions, SDK selection and clean exits |
| GameRegression27 | 66 passed: 60 clean, 6 warning tests | Existing native game tests still pass; warning test paths exactly match GameRegression12 |

The warning test paths are BottlePawnPhysics.Heavy, BottlePawnPhysics.Lightweight, AuthoredBottleGrabRelease, LegacyPlasticBottleMigration, PlasticPressure and ManualWheelRotator. Passing with existing warnings does not mean those warnings are resolved.

The latest checks use NullRHI and separate test user directories. They do not render game graphics or establish visual quality, audible voice quality, or packaged-game behavior. The game's real save files are checked separately against the deployment baseline.

## Live Epic service evidence and limits

Earlier distinct-account Harness logs established Account Portal login, public lobby discovery/join, an EOS game connection, RTC room admission and visible ready-state synchronization. They do not establish acceptance of the actual game map.

The new headless runs establish real Epic login, PUID availability, Epic friend queries, voice mute/volume controls, public lobby creation and public discovery. A host attempt encountered EOS lobby update/destroy timeouts; its log is retained, and a subsequent host creation succeeded. These service errors are recorded rather than hidden by passing compilation results.

One two-process attempt resolved both roles to the SAME product user. It produced RTC duplicate_session errors and an EOS self-connection timeout. That attempt is invalid as a two-player transport test. The final run used distinct product users, enforced by an explicit test assertion, and passed. Account IDs and credentials are not copied into this report.

The opt-in `CrossplayChecks` DeveloperTool module resides in the isolated workspace, not in the installed game plugin. It directly drives the C++ facade and records assertions for public create/update/search, reliable chat/ready, client permission rejection, owner start, actual game-map travel, real ACharacter movement, reconnect, private invitations/join/kick, leave and logout. A compiled test driver is not a passing result; current results are recorded in `Saved/CrossplayIntegration/EpicServiceEvidence/GameIntegration.json`.

Actual game-map EOS travel, character movement and the private invite/kick/reconnect/logout lifecycle now pass the final distinct-account run described below. Audible RTC, different-network NAT/relay, token expiry, loss/recovery and long runs still require acceptance. Steam setup/testing remains deferred. The existing portable release compiled Editor/Development/Shipping before this runtime patch; the latest patch has been rebuilt for Editor only, so final standalone Development/Shipping packaging must be refreshed before distributing that revision.

## Development use without new UI

Launch the game through Unreal with `-game -CustomConfig=Epic`. Use the console:

1. Both players: `Crossplay.Run login`; finish Epic sign-in with DISTINCT accounts.
2. Host: `Crossplay.Run host Cola shop`.
3. Guest: `Crossplay.Run find`, then `Crossplay.Run status` to see result indices, then `Crossplay.Run join 0` using the selected index.
4. Both: `Crossplay.Run ready`. Host: `Crossplay.Run start`.
5. Inspect `Crossplay.Run status`. Use `chat <text>`, `leave`, `reconnect`, `logout`, `mute` or `unmute` through the same command.

These are asynchronous requests. An accepted return is not service success: production integration must observe OnOperation/OnStatus and world connectivity. The optional existing Crossplay.Menu remains available but has not been redesigned. The microphone is muted by default.

For GPU-limited functional checks, the isolated driver uses `-nullrhi`, disabled overlay and browser authentication. This exercises networking/game logic without a game renderer; it does not replace a later rendered playtest. Official Epic documentation describes browser Account Portal authentication when overlay is disabled: [Online Subsystem EOS](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine).

## Final current-project EOS acceptance

Both roles passed in `Game28_220634`: **29 host assertions and 25 client assertions**, with no failed assertions. Two distinct Epic product identities were confirmed. Tests ran against the current prototype map, with all 2,384 game source/content files hash-matched to the live project before the run. The six source differences found during the earlier comparison were encoding/newline differences; refreshing the isolated copy changed no live game files. BuildCurrentGame34 passed. The tested runtime DLL is byte-identical to the DLL installed in the actual project.

Verified through real EOS services: Epic authentication and product identity, friend reads, mute/volume controls, public lobby creation/discovery/join/update, hosted discovery preserving phase, EOS listen transport, authoritative chat sender attribution, bidirectional chat, ready synchronization, rejected client start/kick, owner start, server/client travel into the Normal Cola prototype, real character movement and server position agreement, locked readiness after start, departure removal, reconnect into the running game, post-reconnect chat, public cleanup, private lobby creation/visibility, an actual private invitation, invited private join/chat, owner kick, kicked-client cleanup, private cleanup and logout/identity clearing.

The first movement fixture injected input through the online polling ticker and failed to exercise CharacterMovement. A separate IP probe reproduced that fixture failure. Replacing only the test input with a transient component ticking before CharacterMovement produced about 996 cm of movement and matching client/server positions; the corrected EOS run then passed as well. The character's gameplay code, movement implementation, size and collision were not changed. Earlier failures remain retained as diagnostic evidence, not accepted results.

RTC room admission succeeded (two host and three client channel connections across the lifecycle), and mute/volume API calls passed. No automated microphone unmute/transmission or audible-audio acceptance was performed. Both final processes exited; neither final log contains a fatal error, GPU allocation failure or reliable-buffer overflow. NullRHI avoided game-renderer GPU use.

This establishes Editor `-game` functional integration in a verified matching test copy plus native/IP/regression acceptance directly against the installed live copy. It does not establish a rendered physical-input playtest, remote checkout/bottle/wallet interaction acceptance over EOS, a newly cooked online package, different-network relay acceptance or Steam crossplay. The latest runtime patch still needs its standalone Development/Shipping package rebuild before distributing that revision; the earlier 0.1.0 package has its own historical build evidence.

The implementation checkpoint and sanitized evidence are saved in `Saved/CrossplayIntegration/EpicServiceEvidence/GameIntegration.json`. All real game source, module manifest, ordinary config and save files still match the pre-patch baseline. Private client configuration selects Dev only and remains ignored by Git. No store publishing, Live deployment settings or OS services were changed.

## Recovery and next validation

The runtime rollback and pre-deployment hashes are in `Saved/CrossplayIntegration/RuntimePatch_20261114_212803`. Private profile backups remain under ignored `Saved/CrossplayIntegration/GameProfile_*`; they must not be copied into a portable release. The current work/checkpoint and direct-test sources are under the original workspace's `Saved/CrossplayWork`.

Next: refresh standalone Development/Shipping packaging; perform a rendered two-player gameplay interaction check; then accept audible voice and different-network/packaged transport. Steam setup remains deferred. The system remains a development/beta integration until those checks pass.
