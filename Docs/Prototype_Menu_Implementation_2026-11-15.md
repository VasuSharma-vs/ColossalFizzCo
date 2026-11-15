# Colossal Fizz Co prototype menu

Status: installed and validated in ColossalFizzCo, 15 November 2026 (local time). Native game/plugin binaries and the dedicated menu map are installed. This is an Editor-based prototype, not a newly packaged game release.

## Opening the prototype

Run `Play_ColossalFizzCo.cmd` in the ColossalFizzCo project folder. It uses the installed UE 5.8 Editor executable in game mode, opens the dedicated menu, and selects the ignored Epic development configuration. Complete Epic authentication in your own browser.

For offline play, run the same launcher with `--offline`. The menu offers **Play solo offline**. The normal project configuration keeps online services disabled; an ordinary offline launch explains why its Epic sign-in button is disabled. This avoids putting service credentials in shared project configuration.

Low graphics is the launcher default: one 1280x720 window, 30 FPS cap, a small texture-streaming pool, no ray tracing, and reduced optional graphics. Use `--quality project` to retain the project's usual renderer. The graphics overrides live in ignored custom launch profiles; they do not alter bottle materials or the default game renderer. `UE_ENGINE_ROOT` or `--editor` can select a different installed engine path.

This is an **Editor-based playable prototype launch**. The older packaged build14120 is preserved and does not acquire the menu automatically. A fresh Development/Shipping game package and packaged cross-network acceptance remain separate work.

## Playing with a teammate

1. Both players launch the same current prototype and sign in with **different Epic accounts**.
2. The host enters a session name, optionally checks **Invite-only session**, then selects **Host session**.
3. For a public shop, the other player selects **Refresh public sessions**, then the appropriate **Join** row. A full session cannot be selected.
4. For an invite-only shop, the host refreshes Epic friends and selects the teammate's invite row. The other player's menu presents **Accept invitation**. A player already in another session must leave it before accepting.
5. Both connected players select **Ready**. Only the host can select **Start prototype**. The server validates ownership/readiness and travels everyone into `Lvl_NormalColaPrototype`.
6. During gameplay, **Escape** opens the session menu. **Resume shop** restores gameplay input; **Leave to main menu** cleans up the online session and returns to the menu. Opening this local menu does not pause the server.

Before Epic brand review, the test accounts must have access to the development product, as in the existing Epic setup. Steam authentication is outside this Epic-only menu. Steam remains available through the reusable plugin when its separate platform setup is completed.

For two-account testing on one PC, prepare the **normal default browser** with the joining account before launching the client. The Epic portal can automatically approve the browser's remembered host account before a sign-in URL is copied into InPrivate. Verify distinct product identities in the QA result; changing a display-name label is not evidence of a second account.

## Architecture and changed files

- `Source/ColossalFizzCo/Public/UI/PrototypeMenuWidget.h` and `Private/UI/PrototypeMenuWidget.cpp`: native UMG login, host/public discovery, join rows, roster, ready/start, Epic friends/invitation acceptance, microphone controls, offline play and leave/resume/quit. Displayed members and ready status come from the existing authoritative lobby state. Network/economy/prop logic is not duplicated in the widget.
- `Public/UI/PrototypeMenuSubsystem.h` and `Private/UI/PrototypeMenuSubsystem.cpp`: local-player presentation, owned Escape input binding, controller/world change handling, invitation presentation and input/focus restoration. It is skipped on dedicated servers, commandlets, NullRHI acceptance peers, or with `-NoPrototypeMenu`.
- `Public/UI/PrototypeMenuGameMode.h` and `Private/UI/PrototypeMenuGameMode.cpp`: minimal lobby game mode and invisible replicated menu pawn. Actual shop gameplay uses the existing shop map/game mode.
- `Private/Tests/PrototypeMenuTests.cpp`: native real-widget construction, service-to-view transition, request retry/focus stability, authoritative ready/owner gating, pending invitation/microphone state and browser/sign-out availability with a leftover standalone roster after cleanup.
- `ColossalFizzCo.Build.cs`: dependency on the reusable `UniversalCrossplay` module.
- `Plugins/UniversalCrossplay/Source/UniversalCrossplay/Public/CrossplaySubsystem.h`, `Private/CrossplayVoice.cpp`, `Private/CrossplayOnlineState.h`, `Private/CrossplaySubsystem.cpp`, `Private/CrossplaySessions.cpp`, `Private/CrossplayNetwork.cpp`: read-only microphone/pending-invitation accessors, incoming native session-invitation delegate lifecycle, stale invitation cleanup after successful join/logout, and a guard against late roster replication reopening a session during/after cleanup. These remain game independent.
- `Content/NormalColaPrototype/Lvl_PrototypeMainMenu.umap`: dedicated lightweight menu world with native lobby game mode and player start. No existing gameplay map, Blueprint, mesh, transform, bottle scale or saved prop state is modified.
- `Config/DefaultEngine.ini`: game/editor startup menu; server default remains the shop. `Config/DefaultGame.ini`: menu and shop cook entries. Ignored Epic profile: menu lobby/return paths, shop gameplay path, updated compatibility tag.
- `Scripts/LaunchPrototype.py` and `Play_ColossalFizzCo.cmd`: credential-free launch helpers. Generated EpicPrototype/PrototypeOffline configuration directories remain ignored, including transient writes.

The existing generic plugin sample menu remains available for plugin demonstrations. The new game frontend is independent presentation in the game module, using the same subsystem API. No Blueprint frontend or plugin-to-game dependency is introduced.

## Validation

Recorded evidence is under `MarryByMidnight/Saved/FrontendWork` in the shared workspace. The native build, rendered solo loop, real two-account flow and installed-copy widget test passed.

- Isolated Editor build: succeeded (BuildMenu12 (production cleanup fix in BuildMenu10; cached source-list refresh and cleanup test compiled in BuildMenu12)).
- Plugin automation: **5/5 clean passes**.
- Game automation: **67/67 passes**, 61 clean and the same six warning-bearing tests as the pre-menu baseline. The new menu test passes without warnings. Existing warnings occur in BottlePawnPhysics.Heavy, BottlePawnPhysics.Lightweight, AuthoredBottleGrabRelease, LegacyPlasticBottleMigration, PlasticPressure and ManualWheelRotator.
- Rendered 1280x720 menu: layout and text inspected; no clipping. The Quit button closed the test process normally.
- Real two-account menu/button flow: **30 host and 26 client assertions passed**, run `Menu_004322`. Distinct Epic users, real menu login/host/public refresh/join rows/ready/start/leave/sign-out bindings, actual shop travel and character movement, reconnect, private visibility, incoming native invitation acceptance, kick and cleanup were verified. One rendered window was used only for the solo check; online peers used NullRHI to preserve GPU memory. No audible voice or packaged-network claim is made.
- Solo gameplay: rendered menu -> shop -> Escape -> Resume -> Escape -> Leave to main menu -> Quit passed at 1280x720. Viewport input-mode restoration was observed; long physical movement was not measured in that visual check. Actual remote character movement was independently checked in the EOS run.
- Installed-copy native widget/action/state test: **1/1 clean pass**, loading the actual live project and installed game/plugin DLLs with an isolated test save directory.

The opt-in developer QA adapter invokes the actual native menu's bound buttons and join rows through reflection. It is not installed in the live game or reusable release plugin. The previous backend acceptance (29 host/25 client assertions) is separate evidence and is not substituted for testing the new menu.

## Preserved state and remaining limits

The pre-menu game module14120, project source/configuration and actual save files were backed up before work. Installation uses an explicit file list and merges only the game module into the live module manifest; the development-only CrossplayChecks module must not be copied into it. The old14120 DLL is retained for rollback. Private Epic values are neither printed nor copied into documentation/release archives.

No storefront publication, portal security changes or Steam setup is performed. Voice room/control APIs are exposed, with microphone muted by default; audible audio acceptance, separate-PC/network testing, invitations between independently packaged clients and a new packaged game build remain pending. Existing gameplay tests passing does not establish that every prototype gameplay feature is production-ready.

The online driver sends a real private invitation to the other owned test account through the plugin API and accepts it through the new menu. It reads Epic friends, but does not substitute that for manually testing the menu's friend-row invitation selection. That manual friend-row check and microphone audio acceptance remain pending.

## Acceptance corrections and backups

The first duplicate-account login attempt was rejected by QA. An early automated session-name entry did not reliably follow the real typing event path; the adapter now explicitly broadcasts the text-change event. Post-kick diagnostics identified the underlying sign-out blocker: EOS reported LeaveLobby EOS_NotFound after the local named session had already been removed, but cleanup left the service in Leaving. Cleanup now treats an already-absent local session as complete; a real retained-session failure restores a retryable lobby state. A new native plugin regression tests missing, repeated, signed-out, retained and removed-record cleanup cases through a local provider. A separate native menu regression prevents leftover standalone rosters from hiding browsing/sign-out, and late roster replication cannot reopen a closed session. The QA driver waits for the actual enabled Sign out button after travel. Failed/incomplete diagnostic runs are preserved and are not counted as passing acceptance.

Live rollback backup: `C:\Users\PC\Documents\Unreal Projects\ColossalFizzCo\Saved\FrontendInstallation\20261115_005840`. Pre-task source/configuration/native-module/save baseline location: `MarryByMidnight/Saved/FrontendWork/BaselineLocation.json`. All backed-up save files, original gameplay source outside the explicit frontend patch, the project descriptor and the old14120 module were hash-verified unchanged. Existing gameplay map/content were not copied or rewritten.

Launcher online/offline dry runs passed; both generated profile directories, including pending files, are confirmed ignored by Git. Targeted git diff --check passed. The new reusable-plugin cleanup regression is in `Plugins/UniversalCrossplay/Source/UniversalCrossplay/Private/Tests/CrossplayCleanupTests.cpp`.
