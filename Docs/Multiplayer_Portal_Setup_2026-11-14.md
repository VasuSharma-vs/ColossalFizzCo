# Epic and Steam development setup in progress

Recorded 14 November 2026 in Asia Kolkata time. The reusable plugin is installed and tested locally. Online mode remains disabled. This checklist records development setup. The owner confirmed creation of the Colossal Fizz Co product under80o-studio. The assistant has not changed portal settings, client credentials or store publication.

Current scope is Epic only, as selected by the owner. The owner reports the existing organization at https://dev.epicgames.com/portal/80o-studio and a Create Product button. The owner has confirmed creation of the Colossal Fizz Co product. Steam onboarding is deferred.

## First step for the project owner

Open the [Epic Developer Portal](https://dev.epicgames.com/portal) and sign in yourself. Open [Steamworks](https://partner.steamgames.com/) and sign in yourself if you already have partner access. Confirm which developer accounts are ready and whether this game already has a Steam App ID. Passwords, OTPs and private keys should stay out of chat.

Browser control could not initialize in this session. Both supported control runtimes returned “failed to write kernel assets”; a reset did not recover access. Consequently, signed-in accounts, products and permissions have not been inspected. No portal success is assumed.

## Private local inputs prepared

The input file is `Saved/CrossplayIntegration/Private/eos-steam.private.json` under this game project. Saved is excluded by the project's existing .gitignore. The file contains a locally generated encryption key and the actual existing prototype gameplay map. Do not upload or commit it, and do not paste its contents in chat. Tell the assistant its path when fields are complete.

Product, sandbox, deployment, artifact and client values remain blank. Steam App ID remains zero. Lobby and return maps remain blank pending selection. The profile generator has not been run, and no incomplete online profile has been activated.

## Epic development product

1. Select the correct developer organization and an existing product for Colossal Fizz Co if one exists. If none exists, choose the intended organization and review the product name before creating it. Keep this separate from public store publishing.
2. In Product Settings, record ProductId. In the sandbox and deployment settings, select development resources and record SandboxId and DeploymentId. Both storefront builds must use the same deployment for these tests.
3. In Clients, prepare the development policy covering Connect, lobbies and lobby-associated voice. Prepare Steam and Epic game-client credentials under the intended policies. Client creation and persistent access changes require explicit approval if performed by the assistant. You may perform those steps yourself in the portal.
4. For Epic authentication and friends, configure the EAS application, client link, Basic Profile, Friends List and Presence scopes, and test-user access. Keep application/brand/publication steps separate from the initial development tests.
5. Put the resulting SteamClientId, SteamClientSecret, EpicClientId and EpicClientSecret into the private file locally. Follow the full setup guide for the exact artifact and profile settings. Do not add Steam publisher keys or RTC administrator credentials to this file.

## Steam dependency

If your Steamworks account already owns the game, record its real SteamAppId and confirm that the two test accounts have licenses. Add Steam under the EOS product's identity providers and match the configured Web API ticket identity. Any publisher key belongs only in Epic's trusted provider configuration.

If you do not yet have Steamworks partner access or an App ID, onboarding and any required identity, banking, tax, agreements or payments are account-owner steps. We can prepare Epic development testing while Steam onboarding is pending. A test App ID alone does not prove this product's Steam to EOS authentication works.

## After the required values exist

1. Choose cooked LobbyMap and ReturnMap paths; GameplayMap already points to the existing Normal Cola prototype. No game map has been altered.
2. Review the private configuration and generate the ignored store profiles using `Plugins/UniversalCrossplay/Scripts/ConfigurePrivateProfiles.py` as described in the full setup guide. Use `--store Epic` now; only the Epic profile is generated and Steam fields are not required. The original paired mode remains available later.
3. Restart with the intended development CustomConfig and authenticate. Start with two authorized Epic test accounts, then validate Steam and the four host/client store pairings once Steam configuration is ready.
4. Record actual public/private lobby behavior, invitations, EOS transport, ready/start travel, voice, text, departure, reconnect and cleanup. Keep each result separate from the previous IP-only tests.

Full instructions: [UniversalCrossplay setup](../Plugins/UniversalCrossplay/Docs/SETUP.md) and [validation matrix](../Plugins/UniversalCrossplay/Docs/VALIDATION.md).

Official references: [Epic portal guide](https://dev.epicgames.com/docs/dev-portal/dev-portal-intro), [identity providers](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/identity-provider-management), [Steamworks onboarding](https://partner.steamgames.com/doc/gettingstarted/onboarding).

## Epic application configuration and first sign in

The owner screenshot confirms the Colossal Fizz Co EAS application shows Permissions and Linked clients as Configured; brand settings remain draft. Dev product, sandbox, deployment and Epic client identifiers are recorded in the private configuration, and the client secret is present without being displayed. Authentication has not yet been verified.

An ignored Epic profile is prepared in the independent Harness project using the existing empty Engine Entry map for all three validation map roles. The actual game lobby and return map choices remain unset. The main game remains offline. The owner must complete the Epic account portal sign-in from the separate multiplayer example window before authentication can be assessed. Two distinct authorized organization-member accounts are required for the multiplayer tests.


## First real Epic login result

The first account completed Account Portal authentication. The SDK log reports EOS_Success and the owner reports Signed in in the plugin UI. This validates first-account authentication in the independent test host, not multiplayer joining or voice. The owner confirms a second Epic account is already an organization member. Two-account lobby validation is next. UE EOSVoiceChat reports deprecated RTC device-API warnings; audio behavior is still pending. Sanitized evidence is saved under Saved/CrossplayIntegration/EpicServiceEvidence.

## Two account EOS lobby result

Two distinct Epic product user identities completed public lobby creation, search, join and an actual NetDriverEOS connection on the same PC. The server log records Join succeeded and the client is welcomed into Entry. Both SDK voice users joined the lobby RTC room; audible voice is not yet verified. Sanitized evidence is TwoAccountLobbyJoin.json under Saved/CrossplayIntegration/EpicServiceEvidence.

The owner reports one-way movement of the bare test map flying balls. This is consistent with its default Pawn and simple FloatingPawnMovement, which lacks CharacterMovement client-to-server move handling. Test-pawn movement is not classified as validated gameplay movement. At the owner request to prioritize important work, no custom movement fixture is added. Bidirectional chat/ready RPCs and eventual actual-game character movement remain the relevant checks.
