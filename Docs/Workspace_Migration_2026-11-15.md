# Workspace migration and safe-deletion checkpoint

15 November 2026. All development material from the former MarryByMidnight folder has been transferred into ColossalFizzCo. The old folder was deliberately left in place, empty of files, for the designer to remove.

## Preserved locations

- `Saved/CrossplayWork`
- `Saved/FrontendWork`
- `Saved/ModularWork`
- `Saved/PortionBug_20261115`
- `Saved/ProductCatalog_20261115`
- `Saved/U11`
- `Saved/U13`

No subset of backups was discarded: all seven folders were moved, including the complete staging copy, reusable plugin source/distributions, QA plugin, test runners, saved-game copies, original ZIP backups, checkpoints and logs. The empty old `.git` folder contained no history or files; the active ColossalFizzCo Git repository was untouched.

Before/after SHA-256 verification covered **10,762 files**, **22,210,744,892 bytes** (20.69 GiB). All moved files were initially byte-identical. Runnable paths and current documentation were then updated; every edited original is retained in the migration ZIPs. The final preservation check accounts for every original file. One generated UnrealBuildTool XML configuration cache refreshed during testing; it is recorded separately and contains no authored work.

## Verification

- The active ColossalFizzCo Editor target compiles directly with the old folder empty.
- Game/editor native checks: **72/72 pass** (66 clean, six existing warning-bearing tests).
- UniversalCrossplay checks: **5/5 pass**.
- Relocated local-IP listen-server, separate client and late-join checks pass; all three processes exit0. No Epic login or rendered window was needed.
- Original game source, assets, configuration and gameplay saves match the pre-move baseline. Generated build outputs are expected to refresh during compilation.
- Online and offline prototype launcher dry-runs pass.
- No active script/project/configuration needs the old filesystem folder. Historical logs, compiled debug/source paths and archived receipts may retain their original text; map their former root to ColossalFizzCo when inspecting them. Unreal `/Script/MarryByMidnight` compatibility redirects remain intentionally intact because they are package identities, not filesystem dependencies.
- The QA plugin remains isolated under `Saved/CrossplayWork/QAPlugins`; it is not enabled in the main game project.

## Current test entry point

Close Unreal before running unattended development checks, then use the engine Python executable to run:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' './Scripts/RunDevelopmentChecks.py' --suite all
```

Available suites are `game`, `plugin`, `network`, and `all`. Native checks target the main project. Network checks use the preserved isolated staging project and QA plugin. Each run writes a unique `Saved/DevelopmentChecks` directory and preserves original gameplay saves. The maintained runners parse successfully; older one-time patch/deploy scripts are preserved for reference and must not be blindly rerun against already-updated source.

## Evidence and backups

`Saved/WorkspaceMigration_20261115` contains InputManifest.json, LiveBaseline.json, MoveJournal.txt, TransferVerification.json, path-update records, original pre-edit ZIPs, BuildEditor.log, and FinalVerification.json. Check `Saved/DevelopmentChecks/Latest.json` for the post-move test run and its detailed report directory.

These archives are local files under the project's Git-ignored Saved directory. Moving them did not create a cloud/Git backup. Keep this material if cleaning Saved in the future; private local profiles and archived credentials must remain out of source control.

The gameplay/visual scope is unchanged. This filesystem verification does not replace pending rendered/manual gameplay, a new packaged build, two-machine/Epic networking or performance acceptance.

**Deletion status:** the old MarryByMidnight folder has no remaining files and is safe to remove. Use ColossalFizzCo as the workspace for future development. No original root folder was deleted automatically.
