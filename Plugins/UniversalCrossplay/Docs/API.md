# API and integration examples

No custom GameInstance, GameMode, PlayerController or GameState parent is required. Enable the plugin and obtain UCrossplaySubsystem from your existing GameInstance.

## Blueprint example

1. On your menu button, Get Game Instance -> Get Subsystem (CrossplaySubsystem).
2. Bind OnStatus, OnOperation, OnLobbies, OnMembersChanged, OnInvite, OnChat and OnVoice.
3. Call Login(Auto) after launching the appropriate store profile. For local Epic development use LoginEpicAccountPortal or LoginDeveloper with the Dev Auth Tool. Steam never falls back to forcing an Epic account portal.
4. Call HostLobby(Name, Private), or FindLobbies -> OnLobbies -> JoinLobby(Index).
5. The host opens the configured lobby with listen enabled. EOS resolves joining players to the host's EOS address. The engine supplies replication and transport.
6. SetReady updates the owned server component. Start through its RequestStartGame function, or call StartGame on the host subsystem. Only the owner with all connected members ready can start; clients cannot bypass that rule.
7. LeaveLobby destroys/leaves the online session and returns to ReturnMap. Logout first leaves an existing lobby. Reconnect resolves the last lobby ID if that host still exists; there is no host migration.

Alternatively, call OpenMenu(PlayerController) from the Crossplay Sample UI Blueprint library, or run Crossplay.Menu. This optional native widget demonstrates sign-in, public/private hosting, discovery/join, invitations, ready/start, chat, microphone mute and leaving. Device/per-player volume controls are facade functions for your own settings UI.

## Development console integration

Development builds also expose `Crossplay.Run` for integration without menu work. Launch with the configured Epic profile, open the Unreal console, and run these requests sequentially. Check `Crossplay.Run status` between asynchronous operations; an accepted request is not proof of completion.

- Both players: `Crossplay.Run login`, then complete Epic sign-in.
- Host: `Crossplay.Run host Evening shop`.
- Guest: `Crossplay.Run find`, inspect the indexed results with `Crossplay.Run status`, then `Crossplay.Run join 0` (use the actual selected index).
- Both players: `Crossplay.Run ready`. Host: `Crossplay.Run start` to travel to the configured gameplay map.
- Other requests: `chat <text>`, `unready`, `leave`, `reconnect`, `logout`, `mute`, `unmute`.

Commands call the existing subsystem and owned RPCs; they do not bypass authority or introduce a separate session implementation. They are excluded from Shipping. Status logs contain counts and lobby titles, not credentials or account IDs. The microphone remains muted unless explicitly unmuted. A game's production controls should bind the Blueprint/C++ API and its completion events.

## C++ example

```cpp
#include "CrossplaySubsystem.h"
auto* Online = GetGameInstance()->GetSubsystem<UCrossplaySubsystem>();
Online->Login(ECrossplayStore::Auto);
// Wait for OnOperation("Login", true), then:
Online->HostLobby(TEXT("Evening game"), false);
// Wait for OnLobbies after FindLobbies(), then select a current result:
Online->JoinLobby(SelectedIndex);
```

Handle asynchronous completion events; a true method return means the request started, not that the external service succeeded. In particular, Join completion accepts the lobby request; world connection/travel can still fail and reports OnStatus/error events. Operation deadlines are bounded using wall-clock time. Do not call host and join concurrently.

## Identity and social

Identity exposes EOS OnlineId, common ProductUserId, display name and detected store. Steam and Epic friend queries are distinct: ReadFriends(Steam/Epic) returns that provider's cached entries. InviteFriend takes an index from the CURRENT returned list. Steam rich-presence callbacks and accepted EOS invitations raise OnInvite; call AcceptInvite explicitly. Launch invitations are retained until authentication is ready.

PlayerState's online ID identifies a server connection. The displayed store label is reported by that connection and is presentation metadata, never authorization. The plugin does not treat the Steam and Epic friend lists as automatically linked.

InviteProductUser(ProductUserId) sends an EOS lobby invitation to an already known cross-store PUID; obtain that identity through your permitted player/social workflow. It does not enumerate strangers or automatically link provider accounts. KickMember(OnlineId) is available to the local listen host: it removes the EOS lobby member and closes that player's game connection after service confirmation. Both operations report asynchronous OnOperation completion and require platform acceptance tests.

## Voice

bEnableVoice requests lobby-associated EOS RTC. OSS automatically logs in the voice user and joins/leaves lobby channels. Its same lobby membership survives server travel. The plugin does not release the OSS-owned user.

Use SetMicrophoneMuted, SetVoiceVolume(0..2), SetPlayerVoiceMuted(PUID), GetVoiceDevices(Input) and SetVoiceDevice(Id, Input). Empty device ID selects the system default. Voice status/device-change events report availability. The default microphone state is muted; OS permissions still apply. Leave/logout cleans up voice membership via the session/identity providers.

## Chat and ready state

UCrossplayPlayerComponent belongs to an owned PlayerController. Only that connection can call its Server RPCs. It enforces 256-character wire payloads, printable nonempty text, monotonic sequence numbers, a burst budget and refill rate. The server supplies sender identity/name from PlayerState, not client text. Chat lines are delivered to the shared lobby actor; clients keep up to50 lines locally. No bulk history is sent to late joiners and no chat is saved to disk.

The optional world integration creates the shared actor and attaches components automatically for an active online lobby/game. Existing gameplay classes are preserved. You can opt into the same components for your own offline/LAN workflow with bAutomaticPlayerComponents. Ready state locks after start. Departing players are removed. Only GameNetDriver is changed in memory for online profiles; unrelated driver definitions are kept.

## Achievements and statistics

WriteAchievement(Store, Id, Percent) and SubmitStat(Id, Delta) call the corresponding provider interfaces. Configure AllowedAchievementIds and AllowedStatisticIds first; an empty allowlist disables writes. Define matching IDs in Steamworks/Epic before testing. There are no game-specific achievements or automatic rewards. These are client-service progression adapters, not an authoritative economy or anti-cheat backend.

## Future dedicated servers

Keep server game rules in your GameMode and replicated game data in your GameState/PlayerState. The plugin's global subsystem owns online lifecycle; its world actor/components handle bounded lobby/chat state without reparenting those classes. Later add a server target, server service identity/admission policy, map provisioning, persistence operations and remote-only tests. Do not call local sign-in, overlay, UI or microphone functions on a dedicated server. The current StartGame/HostLobby flow is intentionally listen-host only.
