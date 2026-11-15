# Integration and release checklist

## New project

1. Copy the complete `UniversalCrossplay` folder into `Plugins` and enable it. Keep the `ThirdParty` and `Binaries/ThirdParty` folders. Build with UE 5.8.2 on Windows x64; other UE minors and operating systems have not been validated.
2. Keep online disabled for the first launch. Confirm the startup log selects EOS 1.19.2.1 and existing offline gameplay still works.
3. Follow `SETUP.md` to configure your own development EOS deployment, Steam app and test accounts. Keep publisher keys and administrator credentials outside game clients.
4. Generate private Steam and Epic profiles. Supply cooked lobby, gameplay and return maps plus a shared compatibility identifier. Add those maps to your project's packaging settings; this plugin does not modify or create maps.
5. Obtain `CrossplaySubsystem` from your existing GameInstance. Use the optional native sample menu or the Blueprint/C++ examples in `API.md`. No custom gameplay parent class is required.
6. Run the native `UniversalCrossplay` tests, then test the four storefront combinations in `VALIDATION.md` with real accounts. Local IP tests cover replication, not EOS service acceptance.

## Listen-host lifecycle

Login -> create or join lobby -> connect to the lobby world -> ready -> owner starts -> seamless gameplay travel -> leave -> return map. Reconnect attempts to find the same lobby while its host remains available. Host departure ends the listen-server match; automatic host migration is not implemented.

The host owns game rules. Clients use their owned PlayerController component for chat and ready requests. Do not put trusted inventory, currency or rewards into lobby attributes or client-reported platform labels.

## Before shipping

- Complete real Steam/Epic authentication, invitation, public/private visibility, EOS transport/relay, RTC and token-refresh acceptance.
- Verify host/client departure, loss of service, travel failure, full/incompatible lobbies and repeated cleanup/recreation.
- Review client policies, application access/brand requirements, store launch configuration and EOS desktop bootstrapper/service prerequisites. No publishing or OS service installation is performed by this plugin.
- Configure `CustomConfig` in the shipping target, cook the selected maps, and validate the actual packaged build on clean machines. A compiled Shipping plugin is not a packaged game test.
- Verify microphone permissions, default mute, device selection and audible voice. Soak-test latency, packet loss, reconnects, memory and bandwidth on different public networks.

## Dedicated servers later

Keep gameplay authority in server GameMode/GameState/PlayerState code. Add a server build target, server authentication/admission policy, hosting/session allocation and server map deployment. Replace the listen-host create/start workflow for that mode; do not invent a local server player or microphone. Validate headless operation with remote-only clients and service-side credentials. None of those deployment steps is part of this listen-server release.
