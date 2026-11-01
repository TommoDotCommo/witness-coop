# Playing over the internet: Steam lobbies and the Steam transport

Players on different networks meet in a **Steam lobby** and their games talk **through Steam**: Steam finds a direct path when there is one and
relays the traffic through its own servers when there is not, so nobody forwards a port. Direct UDP (an address, a port and a code) stays for a LAN
and for the test lab: "Host direct (LAN)" in the launcher's Lobby tab. A game never switches from Steam to UDP by itself.

## What the player does

1. Start Steam and log in. Open `witness_launcher.exe` (in the game folder), Lobby tab.
2. The host clicks **Host on Steam**. The launcher creates a friends-only Steam lobby for 16 players and shows its number.
3. The host invites: **Invite (overlay)** opens Steam's invite dialog when the overlay is available to the launcher; **Invite friend** sends a
   Steam chat invite to the friend picked in the list (online friends; no overlay needed); or the host reads out the lobby number.
4. A friend accepts the invite in Steam (chat invite, or **Join Game** on the host in the friends list). If the launcher is open it joins at once.
   If nothing is open, Steam starts The Witness with `+connect_lobby <number>`; the mod sees that, starts the launcher with the same words and closes
   that game before it opens a window; the launcher joins. A friend can also paste the lobby number into the Invite field and click **Join**.
5. Everything else is as before: chat, voice (relayed by the host), ready, the host's choices, start. Each launcher writes its game's cfg with the
   others' SteamIDs and starts the game; the games talk through Steam.

## How it works

* **No Steam SDK, no Steam DLL of ours, no app-id override.** The game ships an old `steam_api64.dll` (FileVersion 3.22.75.80, interfaces
  SteamClient017, SteamNetworking005, SteamMatchMaking009; no flat ISteamNetworkingMessages). Its exported `SteamClient()` returns the installed Steam
  client's interface, which hands out current interfaces by name (`SteamNetworkingMessages002`, `SteamMatchMaking009`, `SteamFriends017`,
  `SteamUser023`, `SteamUtils010`). `common/steam_abi.h` declares only the slots used (x64 ABI; ported from the MDK2 co-op mod, same author, MIT).
* **The launcher** (a separate process in the game folder) loads the game folder's `steam_api64.dll` and calls `SteamAPI_Init` with the environment
  variable `SteamAppId=210970` set, as Steam does when it starts a game. Nothing is written into the game folder (no `steam_appid.txt`).
  It checks that Steam is running, that the app is 210970 and that the user is online.
* **The game** inherits that environment from the launcher, so the game's own `SteamAPI_Init` (the game calls it at start; it imports
  `SteamAPI_Init`, `SteamAPI_RunCallbacks`, `SteamAPI_Shutdown`) succeeds. The mod in the game borrows the game's connection
  (`proxy/steam_net.cpp`): it never initialises or shuts down the API; it waits up to a minute for `SteamAPI_GetHSteamUser()`.
  A game started *without* that environment (from a plain shortcut, or by the launcher before Steam lobbies) has no Steam connection at all.
* **Lobby keys**: `witness_mod=witness-coop`, `witness_protocol=lobby2-net1` (the launcher's lobby protocol and the games' peer syntax), and
  `witness_code` (the join code the lobby logic checks). A joiner refuses a lobby without the mod key, and one with another protocol, with a message
  naming both versions. The host's launcher sets rich presence `connect` to `+connect_lobby <number>` so Join Game works.
* **Membership**: the Steam lobby's member list is the door. The launcher's Steam transport only sends to lobby members and drops what anybody else
  sends (`wlobby::Gate`); session requests from non-members are refused. In the game, the roster is the cfg the launcher wrote; a datagram is taken
  only when it comes from the SteamID of the player it names (`proxy/core/peers.h`).
* **Messages**: `ISteamNetworkingMessages`, unreliable + no Nagle + auto-restart-broken-session, one virtual channel per process (the launcher's lobby
  on channel 1, the game on channel 2), every packet class keeping its own magic. The sync core's events, chat, voice and pictures keep their own
  acknowledgements and resends exactly as over UDP: Steam adds no second layer of reliability. Voice in the lobby is still relayed by the host.
* **The Steam overlay in the game**: once the game's Steam API is up (started from Steam, or from the launcher's Steam lobby), Steam puts its overlay
  (`gameoverlayrenderer64.dll`) into the game, and into the launcher too. The overlay inline-patches dxgi's `Present` *and* the function the swap
  chain's vtable points to (the mod's `HookPresent`) and calls one saved original from both; before 2026-11-01 the two called each other until the
  stack overflowed (`0xC00000FD` a few seconds after the first frame, every time the game rendered with the overlay; the earlier "starting it from
  Steam works" had only been seen at the game's config window). The mod now notices being entered again from inside itself and then calls dxgi's
  `Present` through its own trampoline, built from the function's untouched first instructions in `dxgi.dll` on disk (`proxy/overlay.cpp`).
  A stack overflow leaves its innermost frames (module+offset) in `witness_crash_stack.txt` beside the game.
* **One process per player on Steam at a time**: while a player's game runs, that player's launcher stops sending and reading Steam messages and
  closes its sessions (its lobby clock stands still, so nobody times out); it carries on when the game ends.

## The cfg the launcher writes (witness_coop.cfg)

| Line | Meaning |
|---|---|
| `peer=<id>,steam:<steamid64>` | another player, over Steam (a SteamID64 of an individual account) |
| `peer=<id>,<IPv4>,<port>` | another player, over UDP (unchanged) |
| `peer=<port>` and `peer_host=<IPv4>` | the old two-player UDP form (unchanged) |
| `transport=steam` | this game uses Steam (the launcher writes it for a Steam lobby, also when nobody else is in it) |

One game uses one transport: a cfg that mixes Steam and UDP peers, names a lobby id or a malformed id, lists one SteamID twice or lists this player's
own SteamID is refused and the game plays alone, saying why. Up to 15 peers (16 players), Steam or UDP alike.

## When something is wrong (what the player sees)

| Situation | Launcher / game says |
|---|---|
| Steam not running | "Steam is not running. Start Steam, log in, and try again." |
| Steam offline | "Steam is offline. Go online in Steam and try again." |
| The game folder has no Steam API | "The game folder has no steam_api64.dll. Steam lobbies need the Steam version of The Witness." |
| A `steam_appid.txt` with another app | "Steam runs this as app N, not The Witness (210970). Remove any steam_appid.txt from the game folder." |
| Overlay not available to the launcher | "The Steam overlay is not available to the launcher: invite a friend from the list, or give them the lobby number." |
| Lobby full | "That Steam lobby is full (16 players)." |
| Lobby gone | "That Steam lobby does not exist any more. Ask the host for a new invite." |
| Not a friend of the host | "Steam did not let you into that lobby (it is for the host's friends)." |
| Not a co-op lobby | "That Steam lobby is not a Witness co-op lobby." |
| The other player has an older or newer mod | "The host has a different version of the co-op mod (X, you have Y). Everyone needs the same version." |
| The host leaves | "The host left the Steam lobby." (no host migration: the host is the world's authority) |
| The game started without Steam | in the game, for half a minute: "Co-op over Steam is off: The game has no Steam connection. ..." |
| A Steam session fails mid-game | in the game, every 5 s while it lasts: "Co-op over Steam: no connection to NAME (Steam says STATE). Steam keeps trying; ..." |

`netstat` (or `steamstat`) in `witness_cmd.txt` logs the transport and each Steam session's state:
`netstat: transport steam up 1 peers 2`, `steamstat: transport steam up 1 app 210970 self_set 1 sent .. received .. send_failed .. dropped .. peers 2`,
`steamstat: peer 2 state connected`.

## Tests

* Game-free: `make -C launcher lobby-test` runs the whole lobby over a fake Steam link (20% loss, 15% duplicates, reordering) with a stranger who
  knows the code but is not in the Steam lobby; mutant `GATE_OPEN` (anybody on Steam is let in) must fail. `make -C proxy/core test` includes
  `tests/peers.cpp`: three production sync cores over a fake Steam link (20% loss, 10% duplicates, reordering) with an impostor claiming a member's
  id and a stranger; the cfg syntax; mutants `STEAM_ANY_SENDER` (a Steam datagram taken from anybody) and `STEAM_ANY_MEMBER` (a member may speak as
  another) must fail. `make -C proxy/core transport-test` runs eight Windows clients through the production `coop.cpp` over UDP.
* Live, one game: `tools/qa.sh steam` (one slot; Steam running and logged in). A) a game started without Steam's environment reports the Steam
  transport down and opens no UDP socket; B) the launcher hosts a Steam lobby whose id, mod key, protocol key, code and member limit read back
  through the Steam API, starts its game from it (`transport=steam`), the mod reports the game's own Steam connection up (app 210970), and the
  launcher is back in the same lobby after the game; the game (with Steam's overlay in it) is still running a minute later. Mutant: a proxy whose
  Present loop with the overlay is not broken (`make -C proxy build/AnselSDK64_PRESENT_LOOP.dll`) must be seen to crash there.
* Not provable on one PC with one Steam account: two members in one Steam lobby, and two games exchanging messages through Steam. That is the
  owner's two-account test below.

## The two-account internet test (two people, two PCs, two Steam accounts that are friends)

About 30 minutes. Both: Steam running and online, The Witness installed from Steam, the same mod release installed with `Witness-Coop-Setup.exe`.

1. Both: open `witness_launcher.exe` from the game folder (not from Steam). Settings page: set your name. Lobby tab.
2. Host: click **Host on Steam**. Expect "Hosting a Steam lobby (number ...)". Note the number.
3. Host: click **Invite (overlay)**. Note whether Steam's invite dialog appears (yes / no / where). Then pick the friend in the list next to it and
   click **Invite friend**. Expect "Invite sent through Steam chat".
4. Friend, with the launcher **closed**: accept the invite in Steam's chat. Expect Steam to start The Witness, the game to close at once and the
   launcher to open in the host's lobby. Note what happened.
5. Friend: click **Leave**, close the launcher, then in Steam's friends list choose **Join Game** on the host. Note what happened.
6. Friend, launcher open: paste the lobby number into the Invite field, click **Join**. Expect both names in the list, then chat both ways, and
   talk (push to talk, `N` by default): each hears the other.
7. Friend: tick **I am ready**. Host: Co-op, New game, **Start the game**. Both games start after the countdown.
8. Play for 15 minutes in the same area: see each other, solve panels both ways (each solve appears for the other), open a door, talk by voice,
   ping and chat (hold F1 for the keys).
9. Both: in the game's folder, write `netstat` into a new file `witness_cmd.txt` (one line), wait 2 s; the mod logs the state of the Steam session.
10. Host: unplug the network for 20 seconds, plug it back. Both: note what the game said and whether the game catches up afterwards.
11. Both: close the game. Expect both launchers back in the lobby with "The game ended."
12. Both: send `witness_proxy.log` (game folder) and a note of steps 3, 4, 5 and 10. The log holds no SteamIDs or addresses of Steam peers.

Open questions this test answers: does a game whose player's launcher also holds the Steam API receive its Steam session reliably (the launcher
pauses its own messages while the game runs; Steam routes a player's P2P sessions per process, which one PC with one account cannot show); does the
overlay invite dialog appear for the launcher's plain Windows window; does Steam start the game with `+connect_lobby` for an accepted invite.
