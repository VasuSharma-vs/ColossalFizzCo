# Universal Crossplay setup

The plugin currently targets UE 5.8 Windows x64 and a two-player listen server. Steam/Epic service acceptance is pending your portal configuration and separate test accounts. The included local tests do not prove storefront crossplay.

## Configure Epic development services

1. Sign in at https://dev.epicgames.com/portal with a developer organization account. Create or select the product for your game.
2. In Product Settings, record the Product ID. Select or create a development sandbox and deployment, then record their IDs. Both storefront profiles must use this SAME product, sandbox and deployment.
3. Open the Clients tab. Create a development client policy for the client-side services used here: Connect login/account creation and external mappings, lobby creation/search/join/update/invites, and lobby-associated voice. Create a client using that policy. You may use separate Steam and Epic clients under the same deployment.
4. Configure an Epic Account Services application for Epic login and friends. Link the Epic client to that application; request Basic Profile, Friends List and Presence scopes. Add your development/test users through the organization or the permitted testing access mechanism. An unpublished application's account access is restricted.
5. Enable voice support for the product/deployment as required by the portal. This plugin uses lobby-associated RTC; it does not need a trusted-server room-token service or RTC administrator credentials in the client.
6. Record the client IDs and client secrets locally. Generate a 64-character hexadecimal client encryption key. Do not post those values in chat or commit the private configuration.
7. EGS launcher testing additionally requires a configured Epic Games Store artifact/build whose launch executable and sandbox/deployment match the EOS artifact. Record its exact artifact name. Keep development setup separate from live publication; review and approve any live/store publishing changes yourself.

For the initial Account Portal or Developer Authentication Tool tests, the UE artifact name is a local configuration label; configuring EOS development login does not require publishing a store build. The future launcher build must use the appropriate matching artifact configuration. Complete the EOS/EAS development setup first.

References: [portal overview](https://dev.epicgames.com/docs/dev-portal/dev-portal-intro), [client policies](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide), [UE EOS configuration](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine), [lobby voice](https://dev.epicgames.com/documentation/unreal-engine/voice-chat-with-epic-online-services).

## Configure Steam

1. Obtain access to your game's Steamworks application and its real App ID. Both Steam test accounts need licenses/development access. Steam onboarding, fees and account permissions are account-owner tasks.
2. In the Epic product's identity-provider settings, add Steam. Configure the App ID and the Steam ticket verification information requested by the portal.
3. Use Steam Web API authentication tickets. This plugin follows the UE setting SteamTokenType=WebApi:<identity>. Choose a ticket service identity and use the same value everywhere. The guide/config example uses epiconlineservices as a configurable service identity, not a game ID.
4. If the provider setup requests a Steam publisher Web API key, enter it only into Epic's provider configuration. NEVER put a publisher key in a packaged game or this plugin's JSON/config files.
5. Configure the Steam app's development launch/build and overlay. The plugin uses Steam friends and rich-presence invitation callbacks, while EOS alone owns the shared lobby and gameplay transport.
6. Steam profiles use bUseEAS=false and bUseEOSConnect=true, with Steam as the native platform service. This keeps Steam authentication accountless with respect to Epic Account Services. Epic profiles use EAS plus Connect. Native Steam and Epic friend lists remain separate.

References: [Epic identity providers](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/identity-provider-management), [Steam authentication](https://partner.steamgames.com/doc/features/auth), [UE Steam setup](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-steam-interface-in-unreal-engine).

## Install locally

1. Copy UniversalCrossplay into YourProject/Plugins/UniversalCrossplay. Enable it and restart/build the project with the UE 5.8 C++ toolchain.
2. The supplied SDK DLL/lib and manifest are in ThirdParty/EOSSDK. The bootstrap selects the project-local 1.19.2.1 DLL before EOSShared initializes. No engine installation files are replaced. Steamworks comes through UE's Steam plugins.
3. Run Scripts/InstallSDK.ps1 if the runtime copy under Binaries/ThirdParty is absent. Do not load a second EOS SDK or call EOS_Initialize/EOS_Shutdown from game code.
4. Copy ConfigExamples/credentials.example.json to a private file outside source control. Fill the fields from the portal and use your own cooked lobby, gameplay and return map package paths. Give compatible Steam/Epic builds the same CompatibilityId.
5. Run Scripts/ConfigurePrivateProfiles.py --project "YourProject.uproject" --config "private.json" using Python 3. It writes ignored Config/Custom/Steam and Config/Custom/Epic profile files. It does not contact or publish to either store.
   To start with Epic alone, add `--store Epic`. This writes only Config/Custom/Epic/DefaultEngine.ini and requires no Steam App ID, Steam client or Steam token identity. Use `--store Steam` later, or omit the option to retain the original paired-profile workflow.
6. Development launches use -CustomConfig=Steam or -CustomConfig=Epic. For shipping, set the corresponding game Target.cs CustomConfig to the selected profile when building/cooking; shipping does not permit overriding this through the development-only command-line switch. This is build configuration, not a dependency on the original game's code.
7. The plugin's online mode changes only the GameNetDriver definition in memory to NetDriverEOS, retaining other driver definitions. With online disabled it leaves the current IP networking workflow intact.
8. Open the sample with the Crossplay.Menu console command or call Open Menu from the Crossplay Sample UI Blueprint library. Bind your own UI to UCrossplaySubsystem if preferred.

Private profiles are generated only when all required fields exist. A missing configuration produces a reported failure, never a fake successful login or lobby. Keep the private JSON and generated profiles out of source control. EOS game-client credentials are shipped only as needed for the intended client policy; Steam publisher and trusted-service secrets are never client configuration.

## Account and invitation behavior

EOS Connect supplies the PUID used by both stores. Engine OSS handles its login lifecycle and token refresh. Accountless Steam users have no Epic friends/overlay access unless they separately use EAS. Account linking/consent is handled by the configured engine authentication flow; this plugin does not silently merge accounts.

For a private Steam invitation, the plugin maps the Steam friend to an EOS PUID, sends a real EOS lobby invitation, then sends the native Steam invitation. The friend must have signed in to this deployment at least once so that mapping exists. Public Steam rich-presence joining does not require an EOS private invitation. Native overlay and private mapping behavior remain pending real-account tests.

Microphones start muted. Windows microphone access/device selection must permit the packaged game. Lobby RTC joins/leaves through OSS; the same lobby stays alive during gameplay travel. Do not call ReleaseUser on the shared OSS-owned voice user.

## Information still needed

- Product, sandbox, deployment and artifact names/IDs.
- Client policy and client/application links; client credentials saved locally.
- Steam App ID and matching Steam identity-provider ticket settings, only when enabling the Steam profile. These do not block Epic-only development.
- Two authorized accounts per storefront and preferably two machines.
- Which maps to use for the lobby and return screen.

No live-service, publishing, billing or sensitive account changes have been made by this implementation.

## Desktop crossplay distribution dependency

Epic's desktop social crossplay features also require the EOS service redistributable/bootstrapper where specified by Epic. This is separate from selecting the SDK DLL inside Unreal. The supplied SDK includes SDK/Tools/EOSBootstrapperTool.exe, EOSBootstrapper.exe and EpicOnlineServicesInstaller.exe.

Scripts/CreateDesktopBootstrap.ps1 invokes the official tool to generate bootstrapper/config files for a selected packaged executable. It never installs the service, launches a storefront, or publishes a build. It writes a relative app path from the bootstrapper to your selected executable; preserve that staged layout and verify launch arguments still reach the game. Configure the appropriate store launch executable and prerequisites only after reviewing the packaged result. Do not claim the Epic overlay accepted until bootstrapper, service installation and account tests pass.

See [Epic redistributable instructions](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/crossplay/redistributable-installer) and [desktop crossplay overview](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/crossplay/crossplay-technical-overview). This implementation does not silently install an OS service or modify live store launch settings.
