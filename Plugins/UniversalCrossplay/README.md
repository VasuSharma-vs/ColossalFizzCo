# Universal Crossplay

Reusable UE 5.8 C++ multiplayer plugin for Windows x64. Development version: platform service acceptance is pending. It has no game class, map, asset or credential dependency.

Start with [setup](Docs/SETUP.md), [API/examples](Docs/API.md), and [validation](Docs/VALIDATION.md).

Features include a GameInstance subsystem facade, EOS authentication/lobby/session adapters, native Steam friends and invitations, engine-managed EOS RTC controls, optional owned PlayerController RPC components, replicated lobby ready/owner state, bounded chat, cleanup/reconnection, and an optional native sample menu. EOS is the single cross-store session/transport provider. No EOSPlus or Online Services v2 backend is mixed into this OSS v1 integration.

The default is offline. Enable the plugin, build, configure private service profiles, then sign in explicitly. Copying this folder to another project's Plugins directory requires no code from ColossalFizzCo.

The bootstrap selects the supplied EOS1.19.2.1 through the engine's eossdkbinary selector before EOSShared starts. Engine EOSShared owns initialization/shutdown; do not initialize a separate SDK. SDK headers/runtime are retained under ThirdParty with a hash manifest. UE public bridge interfaces were built against its bundled1.19.1.2 headers; runtime compatibility is checked by independent tests.

A listen server is the only hosting mode implemented. Dedicated-server build targets, hosting infrastructure and host migration are not included. Server chat/ready validation has no local-user, UI or microphone dependency. Dedicated-server work later needs separate authentication/hosting policy, build packaging and real remote-only validation.
