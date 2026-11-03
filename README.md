# witness-coop

Co-op for The Witness (Steam app 210970, build 11370412). **Dev preview, not playable with friends yet.** Two copies of the
game on one PC can see each other as puppets, and when one player solves a panel the other game marks it solved and opens the
door it gates. The first two tutorial door puzzles and a panel-to-panel power-on chain have automated bots; power-off switches, lasers, plates
and bridges are not synced. Installing is a single file, `Witness-Coop-Setup.exe` from Releases (see Install below).
Built with the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook; research notes live in
`games/the-witness/NOTES.md` there.

No game files are in this repo. You need your own copy of The Witness, Steam build 11370412
(`witness64_d3d11.exe` SHA-256 `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`).

[![Historical group showcase with name tags and pings](docs/showcase/lineup_a.jpg)](docs/SHOWCASE.md)

**[See the showcase](docs/SHOWCASE.md)** for pictures of every feature.

## What works

The adventurer has [24 procedural emotes and proximity speech gestures](docs/GESTURES.md), with game-free pose checks, sync simulation and a software preview. Hold `H` for the wheel, use `Tab` to change its page, and hold `F1` for the binding guide. Live visual quality remains unverified.

Chat, pings, emotes, drawing, voice (with channels: proximity, radio, an open line, one on one), see-through players, a race mode, a lobby in the launcher (invite friends, see and talk to them before and after the game), saves that say who played and a new game that starts with chosen parts of the map done are described in [features.md](features.md); pictures of all of it are in [the showcase](docs/SHOWCASE.md).

- **Player model.** Each player sees the other as an animated, skinned character (glTF `.glb`), drawn into the game's frame by a
  hook on the Direct3D 11 `Present` call. It stands at the other player's feet, faces where they look, plays a walk clip at a rate
  matched to their measured speed (and an idle or run clip when the model has one), and is hidden by walls and doors: the proxy reads
  the game's own depth buffer (4x MSAA, reversed depth stored as `0.0597 / distance`, found by watching which depth view the game
  binds) and discards the model's pixels that are behind it. The original adventurer is the only character, with idle, walk and run clips and four fidgets.
  It blinks, glances at a player who comes near, carries its head where its player looks, breathes (harder after a run), swings its scarf and
  gear and coat as it moves, acts with its brows in the emotes, is lit with a normal map, roughness and metalness and a sun shadow on the ground, and its footsteps are heard where it walks: [the life layer, shading and shadow](docs/LIFE.md).

  ![The adventurer in the garden](docs/showcase/adventurer_garden.jpg)
  See [asset credits](assets/models/CREDITS.md) and the [migration details](#character-migration) below.
- **Shared panels and doors.** A detector watches all 594 `Machine_Panel` and 327 `Door` entities. A solved panel is sent to the
  other game and written there as solved; an opened door is opened there by calling the game's own door function, so the
  door animates and collides as in single-player. Events are numbered, received in sequence and resent until acknowledged.
  If an entity is temporarily missing or a door call is refused, its acknowledged event stays queued and retries on the render
  thread; other entities can still progress. Successful changes are applied once. A restarted game gets the whole state again.
  The restart catch-up scenario checks this after the original panel/door events have already been acknowledged.
- **Panel power-on progression.** Activating a panel is a separate reliable event from solving it. The peer calls the game's
  panel power-on function on the render thread; its normal frame updates animate the brightness. A powered target remains
  unsolved. Power state participates in startup stability, has its own echo guard, and is included in restart catch-up.
  This currently shares power-on progression; power-off switching is not replicated.
- **Safe start.** Nothing is detected, resent or applied until the world is stable (camera valid and identical panel and door
  counts on three scans three seconds apart); after applying a remote change the entity is suppressed for 5 s so it is not
  sent back.
- **Optional Back TV ("POV TV").** A small CRT TV strapped to the adventurer's back displaying that player's point of view to nearby players as a 128x96 snapshot stream (5 Hz, RLE compressed) with dynamic harness sway, antenna wobble, and phosphor power-on/off animations. Double opt-in: viewer toggle (`pov_tv=1`, F10) and streamer consent (`pov_tv_consent=1`, F11) default off; on-screen HUD indicator signals broadcasting. See [POV TV design and verification](docs/POV_TV.md).

Not done: power-off switching, plates, lasers, bridges, a real network (localhost only), late join into a game
with progress (restart catch-up, a first-time late join and a same-process save reload are in the suite), puzzles beyond the first two tutorial doors, the first black/white tutorial panel and one cable-driving maze (`0x293`), crouch/prone poses (the game has none), lighting that matches the game's, models with several textures or materials, and shadows.

## Quick guide (hold F1)

Hold **F1** in the game or launcher to show a three-column guide; release it to return.
The in-game guide and menus use a proportional Segoe UI font from a padded 2x atlas,
measured wrapping, warm surfaces and amber selection borders. Chat wraps to its panel,
and long input scrolls to keep the cursor visible. `make -C proxy/ui test` checks text
metrics, wrapping, fitting and guide bounds with three mutants, and the dev map's detail pane
(`proxy/ui/detail_layout.h`: every line and button of a selected panel inside its pane, the text at
10 px or more, at eleven screen sizes from 1024x768 to 3840x2160 including 21:9) with two mutants. The focused live layout
check is `tools/qa.sh probe tools/qa_cozy.py`; it renders the live UI scene into D3D11
render targets (over a copy of the current game frame) at
1024x768, 1280x720, 1920x1080, 2560x1440, 3440x1440 and 3840x2160. Add `--native`
to capture the actual 1280x720 game back buffer instead; larger native windows can be
limited by the desktop work area.

It covers the current mod bindings, co-op and race features, host/join flow, shared state,
networking and troubleshooting. The game uses its existing text renderer; both views share
`common/guide.h` and `common/bindings.def`. Configurable keys come from that binding table,
including custom keys; the launcher shows its current settings (restart the game to apply).
F1 works in chat, menus and launcher text fields too, and losing focus dismisses the guide.

This revision had no default F1 action. An existing custom `key_*=F1` assignment moves to
**F2**, leaving F1 for help. If F2 is occupied, it uses the first free key in F12,
then 0 through 9. The guide shows the resulting key. The launcher loads this migration into
its settings fields; Save settings persists it. The DLL also applies it when reading an old cfg.
The launcher rejects duplicate keys, fixed mod shortcuts and WASD for configurable mod actions.

`make -C common test` runs the game-free guide/binding tests and four mutants: omitted key,
colliding keys allowed, F1 migration disabled, and toggle instead of hold. It checks custom
bindings, repeat/release/focus loss and the three-column line budget. Build both views with
`make -C proxy` and `make -C launcher launcher`.

The game opens on the monitor the launcher is on (windowed: centred there; full screen: that monitor). The launcher
passes the work area of its monitor in `WITNESS_MONITOR`, and the mod places the game's window when the game creates it.
`tools/qa.sh launcher` checks it with the launcher moved off the primary monitor, and a mutant launcher that does not pass it must fail.

## Install and play with a friend

Players do not build anything: [Releases](https://github.com/TommoDotCommo/witness-coop/releases) has **one file**,
`Witness-Coop-Setup.exe`, that carries everything (and, for manual installs and Linux, a zip with the same files).
Setup finds The Witness in the Steam libraries (or takes a folder), checks that the exe is the supported build (SHA-256 above;
any other build is refused with a plain explanation and nothing is changed), and adds the proxy `dsound.dll`, `witness_launcher.exe` and
`witness_models\`. **No file of the game is renamed or changed**: the game imports DirectSound (`dsound.dll`) but does not ship it, so Windows loads the
mod's copy from the game folder, and the mod hands every DirectSound call to Windows' own `dsound.dll` (loaded by full path from the system folder).
Every file Setup writes gets a `.coop-receipt.json` beside it, so Uninstall removes exactly what it installed and never a file it did not;
your co-op settings (`witness_coop.cfg`), the logs and the saves stay. A Steam update or *Verify integrity of game files* leaves `dsound.dll` alone (Steam does not know it).

- **Nothing is written into the game folder while you play.** Your co-op settings (`witness_coop.cfg`), the proxy's log (`witness_proxy.log`), the update
  schedule and the race result live in `%LOCALAPPDATA%\witness-coop\`. A `witness_coop.cfg` that an earlier version left beside the exe is moved there once
  (copied, compared, then removed). One exception, for test tools and the lab's per-instance copies: a `witness_coop.cfg` beside the exe that carries one of the
  instance keys (`save_dir`, `force_active`, `run_unfocused`, `diag_dir`) is an explicit per-instance override; then all of those files stay beside the exe, exactly as
  before, so two copies on one PC (or a copy next to a player's own) keep separate settings. The mod never creates that file. (`common/userdata.h`, tested by
  `make -C common userdata-test`.)
- **Mod managers.** The package has `coop-mod.json` (what it is: id `witness-coop`, Steam app 210970, the supported build with its SHA-256, the proxy and the
  chain file, the Proton override `dsound=native,builtin`, and every game-folder file with its SHA-256) at the zip root and in its top folder, and every
  `.dll` and `.exe` carries a version resource (product name *The Witness Co-op*, the release version). Copying the files of the package into the game folder
  is a complete install. Setup tells who put them there: **Setup** (receipts beside the files), **Vortex** (a managed file with more than one hard link, no
  receipt: Setup shows *Installed by Vortex: update or remove it there*, changes nothing and still offers Play; the launcher says the same instead of
  updating), or **by hand** (the files match a release, no receipt: Setup adopts them with receipts, after which they update and uninstall like any Setup
  install). The release script also writes `NEXUS-<version>.txt`, the copy note for the identical zip on Nexus Mods.
- **Setup from a script.** `Witness-Coop-Setup.exe status [--json] [<game folder>]`, `install <game folder> --yes`, `uninstall <game folder> --yes` (the same
  words in `install-linux.sh`) run without a window and end with: 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running.
- **Setup window.** Every install found (Steam libraries via `libraryfolders.vdf` and `appmanifest_210970.acf`, GOG by registry and known folders, Epic by its
  manifests, the uninstall list) is a card with its store, the build check and who installed co-op; Browse finds others. Three ticked options: a desktop
  shortcut, Windows Firewall rule *The Witness Co-op (UDP)* for the game and the launcher on Private and Public networks (one administrator prompt), and
  *Open invite links in the launcher* (a per-user `witness-coop://` handler that starts `witness_launcher.exe --coop-invite "<link>"`; the launcher puts the
  invite into its join box and joins nothing until you press Join). Uninstall removes all three. The last screen's primary button is Play. Tests:
  `tools/test_setup_options.ps1` (fake `netsh`, a throw-away registry key), `tools/test_setup_verbs.ps1`, `tools/test_mod_manifest.py`, `tools/qa_installer.sh`.
- **Another mod named `dsound.dll`** (an ASI loader, an audio wrapper) found in the game folder is kept as `dsound.chain.dll`; the co-op proxy loads it right
  after itself, so its start-up work still runs, and logs `chain: loaded dsound.chain.dll`. The game's DirectSound calls still go to Windows' own
  `dsound.dll`, not to the chained file: a full DirectSound replacement chained this way gets no calls (rule chosen for safety: a chained proxy that loads
  `dsound.dll` by name would otherwise call back into the co-op proxy). Uninstall puts that file back as `dsound.dll`.
- **Coming from v0.1.0-alpha** (the proxy in `AnselSDK64.dll`, the game's own file renamed `AnselSDK64_real.dll`): Setup, `install-linux.sh` and the launcher's
  updater put the game's own `AnselSDK64.dll` back first (only after checking its SHA-256 `3b0677ae...`; a staged copy, one replacing rename; an interrupted
  run finishes on the next one), then remove the old proxy and its receipts. A file whose hash is not the game's stops the migration with a plain message and
  nothing is changed: *Verify integrity of game files* in Steam fixes it. If the new files are copied over an alpha install by hand without that step, the
  game would load both copies of the mod: the new `dsound.dll` sees the old proxy already mapped and stays passive (it only passes the sound calls on and
  logs `OTHER COPY`), so the old copy runs co-op alone until Setup finishes the move.

The one-file Setup checks its embedded payload against a SHA-256 compiled into it, unpacks it into a fresh
`%LOCALAPPDATA%\witness-coop\setup\<version>\` (created exclusively: an existing folder is never reused, a link anywhere on the way is
refused), runs the PowerShell in `dist/` (`installer-gui.ps1`, `witness-coop.ps1`, `install-state.ps1`; the pure decisions are
`dist/install-policy.h`) from there and removes the folder when the window closes. `Setup.exe` inside the zip is the thin version of the same
program: it runs the scripts beside it, and when they are missing (it was started from inside the zip) it says so and points to the one-file download.
PowerShell is started with `-ExecutionPolicy Bypass` for that one process, because Windows' default policy refuses every unsigned script;
the scripts are readable, and signing the exe and dll files (below) is what makes the download trustworthy to Windows.

Then both players open the launcher (or press Play in Steam: a game Steam starts on its own hands over to the launcher) and play from its **Play** tab.
The player's own copy of the instructions is `dist/README.txt`.

### The launcher

Four tabs, the same in every co-op mod: **Play**, **Saves**, **Settings**, **About**.

- **Play**: your name, three choices (*Host a game*, *Join a friend*, *Play solo*) and one button that says what it does (*Open lobby*, *Join*, *Play solo*);
  the last choice is remembered (`launcher_choice=`). Hosting or joining turns the tab into **Lobby**: players (host mark, ready, speaking light, mute me,
  deafen) | chat | the invite and *What to play* (the host chooses co-op or race, a new game, parts of the map done). The host's button reads *Start game*
  or *Waiting for <names>*; the others tick *Ready*; *Leave* is one click. An available update is a banner on Play (*Update*, *What's new*), never over a lobby.
- **Saves**: the co-op saves (who played, progress, lasers, time) and *New game...* (the parts of the map that are done at the start).
- **Settings**: sections in the shared order (Co-op rules, Players and interface, Voice and sound, Controls, Display, Network, Launcher). Every change is
  saved at once (no Save button); *Reset section* puts a section back to its defaults. Volumes, ranges, the frame limit, FOV and brightness are sliders with
  units; keys are set by pressing them (F1 and a key another action uses are refused, with the reason); only names, addresses and ports are typed. The game's
  own options (mouse, FOV, brightness, ...) live inside a save: they are marked *(in a save)* and change the save chosen in *Save to change*. The player
  number is gone from the settings: the lobby assigns it. *Show the launcher at start* off lets Play in Steam start the game directly (hold Shift while it
  starts to get the launcher).
- **About**: version and updates, the quick guide, *Send diagnostics*, links, credits, the game folder.

Keyboard: Enter, Esc (back), Tab, Ctrl+Tab (tabs), Ctrl+Enter (the page's main button), hold F1 (quick guide). A pad (XInput: Xbox pads, the Steam Deck's
controls) works too: stick or D-pad move, A selects, B goes back, LB/RB change tabs, Start presses the page's main button, X and Y are the actions the footer
names. The window is resizable, opens at 1280x800 (or the screen when that is smaller) and fills the screen on a Steam Deck. The shape is in `launcher/shell.h`,
tested game-free by `make -C launcher shell-test` (with mutants) and on the real window by `python3 tools/qa_launcher_shell.py` (no game slot; a
launcher that does not save on change must fail it); `tools/qa.sh steamstart` (one slot) checks Play in Steam handing over; the launcher's command file (`--cmd-file`: `tab`, `section`, `choice`, `pad <buttons hex> <lx> <ly>`,
`capture <key> <vk>`, `setting <key> <value>`) drives it in tests.

### Updating

Every player must run the same mod version. The version (from `git describe`, the release tag) is stamped into the launcher and the proxy by the Makefiles. It shows in the launcher's
window title, on the Play page under *About and updates*, and next to every name in the lobby list. A player on another version cannot join: the host answers with its own version and
yours (*"The host is on v0.2.0, you are on v0.1.0: update both to the same version."*) and the lobby page shows an **Update the launcher** button. A launcher of an older lobby protocol is told to update.
If two games with different versions are started without the lobby (a hand-written `witness_coop.cfg`), the first packet of the other version from a configured peer is said once
in the game and the log (*"Player 2 is on v0.1.0, you are on v0.2.0: update both"*; the launcher's roster supplies the version, `peer_version=<id>,<version>`) instead of being dropped in silence.

**The launcher updates itself from GitHub releases** (`launcher/update.h`, `update_win.h`; ported from the MDK2 launcher's updater). At start it asks for the latest release of
`TommoDotCommo/witness-coop` (at most once a day; nothing about you is sent: one plain request for the public release list, and one per file if you press Update; failures are only written to the
launcher's log). When a newer release exists the Play tab shows *Update available: vX (first line of the notes)* with **Update**, **What's new** and **Later**; the About tab has the full notes, a *Check for
updates* button, and the two settings (Settings, Launcher) (also in `witness_launcher_update.cfg` next to the launcher: `update_check=0` turns the automatic check off, `update_prereleases=auto|1|0`;
*auto* offers pre-releases only to an installed pre-release). **Update** downloads `witness-coop-<tag>.zip` and its `.sha256`, checks the size and SHA-256, opens the archive in memory
(CRC of every file, no path may leave its folder, the executables must be 64-bit), and then replaces `dsound.dll`, `witness_launcher.exe` and `witness_models\*` through **the
installer's own receipt rules** (`dist/install-policy.h`): a file is replaced only if its `.coop-receipt.json` says the installer put exactly that file there, with the receipt journal
(`pending`, replace, `installed`) the installer writes. A copy that was not installed by Setup, a file changed by hand, a leftover `.coop-stage` or an unsettled earlier update are refused
whole, with a plain message and nothing changed. All files are staged and backed up first and put back together if anything fails; if the launcher is killed half-way the next start rolls
back (`witness_update.state`). The journal, the backups (`.coop-prev`) and the old launcher live in the mod's data folder (`%LOCALAPPDATA%\witness-coop\`), not the game folder; only the
short-lived `.coop-stage` files sit beside the files they replace, and an update an older build left half-done in the game folder is still rolled back. The proxy DLL is replaced only while the game is not running; the running launcher is renamed to `witness_launcher.exe.coop-old` and replaced (reopen it after the update; when the data folder is on another drive it stays beside the game until the next start).
No file of the game is ever replaced; a folder still in the v0.1.0-alpha layout is migrated as Setup does (the updater adds `dsound.dll`, then puts the game's own
`AnselSDK64.dll` back, verified, only while the game is closed; a hash that is not the game's is refused and nothing changes). A development build (a version like `v0.1.0-3-gabc1234`, or a dirty tree) neither
checks nor updates. A downgrade (a release that is not newer) is refused. Tests: `make -C launcher test` (includes `tools/test_launcher_update.py`: fake GitHub responses, corrupt and
truncated archives, hash mismatch, interrupted apply, downgrade, the old-layout migration, 39 source mutants), `python3 tools/test_launcher_update_win.py --mutants` (the Windows code against a local fake release server),
`python3 tools/qa_lobby_version.py` (real launchers, three versions), `python3 tools/test_update_installer.py <game build folder> --mutant` (a folder installed by the real PowerShell installer is updated by the Windows updater; the new package's installer must then call every file current and uninstall must restore the original). The maintenance rules for publishing a release the updater accepts are in [docs/UPDATER.md](docs/UPDATER.md).

### Play over the internet (Steam, no port forwarding)

With Steam running, the host chooses **Host a game**, *Through Steam*, and **Open lobby** (a friends-only Steam lobby for up to 16) and invites friends through the Steam
overlay, through a Steam chat invite from the launcher's list of online friends, or by giving them the lobby number; a friend accepts
in Steam (or **Join Game** in the friends list, or pastes the number and clicks Join). The lobby and then the games talk through
Steam, which relays the traffic when there is no direct path, so nobody forwards a port. The launcher starts the Steam API as The
Witness (app 210970) through the `SteamAppId` environment variable, which the game inherits; the mod uses the game's own Steam
connection and never falls back to UDP by itself. Details, the cfg syntax (`peer=<id>,steam:<steamid64>`, `transport=steam`), the
messages a player sees when something is wrong, and the two-account test: [docs/STEAM.md](docs/STEAM.md).

**Direct (LAN / manual)**: *Host a game*, *Direct* gives the invite `address:port/CODE`; the host's lobby port is UDP 47100 and the
games use 47001 and 47002, so over the internet every player would need reachable ports. Use it on a LAN.

### Manual install (no Setup)

Close the game; open its folder (Steam: Properties > Installed Files > Browse, the folder with `witness64_d3d11.exe`).

1. Copy `dsound.dll` (the proxy), `witness_launcher.exe` and the folder `witness_models\` from the zip into the game folder. Nothing is renamed.
   (If the folder already has a `dsound.dll` of another mod, rename that one to `dsound.chain.dll` first: the proxy loads it after itself.)
2. Start `witness_launcher.exe` there (or press Play in Steam: the game hands over to the launcher).

Coming from v0.1.0-alpha by hand: delete the old proxy `AnselSDK64.dll` and rename `AnselSDK64_real.dll` back to `AnselSDK64.dll` (or let Steam's
*Verify integrity of game files* restore it), then step 1.

Uninstall by hand: delete `dsound.dll`, `witness_launcher.exe` and `witness_models\` (your settings and logs in `%LOCALAPPDATA%\witness-coop\` may stay; rename a
`dsound.chain.dll` back to `dsound.dll`). A later Setup can adopt a hand install: it backs up what it replaces.

### Steam Deck and Linux (Proton): untested

Not run on a real Steam Deck or Linux PC yet (tracked in Known issues). What is expected, from how Wine and Proton load DLLs:

- `install-linux.sh` in the zip is the manual install as a POSIX script with backups and the same receipts as the Windows Setup (either can uninstall
  the other's install): `sh install-linux.sh install|uninstall|status [game folder]`. It finds the game in the usual Steam folders
  (`~/.local/share/Steam`, `~/.steam/steam`, the Flatpak Steam, other libraries from `libraryfolders.vdf`), refuses any other build in plain words, refuses
  symbolic links in the way, migrates a v0.1.0-alpha install and keeps another mod's `dsound.dll` as `dsound.chain.dll`, like Setup.
- **The DLL override is written for you.** Wine has its own built-in `dsound`, which it prefers to one in the game folder, so Proton must be told to load the
  mod's: `install-linux.sh install` writes `"dsound"="native,builtin"` into `[Software\\Wine\\AppDefaults\\witness64_d3d11.exe\\DllOverrides]` of the
  game's own prefix, `<library>/steamapps/compatdata/210970/pfx/user.reg` (only for the game's exe; a backup `user.reg.witness-coop-backup` beside it; the
  earlier value is kept in `witness-coop-proton.receipt` in the game folder, and `uninstall` puts exactly that back). Run it with the game closed (Proton rewrites
  `user.reg` when the game exits). The prefix exists only **after the game has run once under Proton**: with no prefix the script says so and prints the
  fallback, the Steam launch option `WINEDLLOVERRIDES="dsound=n,b" %command%` (The Witness > Properties > Launch options). `status` reports whether the
  override is in place. `tools/test_install_linux.sh` tests all of it on fixture folders and fixture prefixes (in WSL) with mutants (an override for the wrong
  exe, an uninstall that leaves it). Untested on a real prefix.
- **Play in Steam opens the co-op launcher**, Game Mode included; no non-Steam shortcut is needed. Steam starts `witness64_d3d11.exe` with its environment
  (`SteamAppId`); the mod sees a game Steam started with no launcher session and starts `witness_launcher.exe` beside it in the same Proton prefix instead,
  then closes the game before it opens a window (`common/handover.h`; the proxy log says `handover: ... -> launcher`). The launcher inherits Steam's
  environment and every game it starts carries `WITNESS_COOP_LAUNCHER=1`, which never hands over again, so the whole session (launcher, game, back to the
  lobby) is one Steam launch: a launcher Steam started runs the game's Steam API itself, because Steam on Windows does not follow the process tree (without
  it Steam records The Witness as stopped once the first game process ends; `tools/qa.sh steamstart --mutant` reads Steam's own record). A Steam invite (`+connect_lobby`) goes to the launcher the same way. *Show the launcher at start* off (`show_launcher=0`)
  makes Play start the game directly; hold Shift while it starts to get the launcher back. On a Deck (`SteamDeck=1`) the launcher fills the screen (1280x800)
  and is driven with the Deck's controls (stick or D-pad, A, B, LB/RB, Start, X/Y as the footer says). Typing (a name, an invite) needs the Steam keyboard
  (Steam + X) for now.
- Unknown: whether Proton's `dsound` loaded by full path from `system32` (what the proxy forwards to) behaves as on Windows, overlay and model drawing
  under DXVK, voice (microphone) under Proton, window placement on the Deck's screen. Reports welcome.

### FAQ: not the supported build

*Setup says my copy of The Witness is not the supported build, or the game says "Co-op is off" when it starts.*

The mod's addresses fit one build of the game only (Steam build 11370412, `witness64_d3d11.exe` SHA-256 `8d672d44...`). Setup (and the launcher) says what it found,
what it needs and what to do:

- **Steam:** right-click The Witness > Properties > Betas > choose *None*, then Properties > Installed Files > *Verify integrity of game files*. Run Setup again.
- **Other stores (GOG, Epic, ...):** not supported yet.
- **Steam already shows the newest version:** the game was updated after the mod. Nothing is broken: the mod notices the unknown exe when the game starts, patches nothing,
  passes every sound call to Windows' own `dsound.dll`, and shows one message. Co-op returns with a mod update.

How the runtime check works: `common/buildid.h` reads the exe's PE header and hashes its code section **in memory** (no file I/O inside `DllMain`); anything
but the known build leaves `proxy/dsound_proxy.cpp` with no hook and no thread of ours touching the game. `tools/test_build_gate.sh` tests the real `DllMain`
with a host program that is not the known build (and a mutant that ignores the check must fail), the forwarding to the system `dsound.dll` by name and by the
ordinal the game imports (a bare-name load must fail), a missing system copy (every call fails cleanly), the chain load (a mutant that ignores the chain file must fail), the
alpha's `AnselSDK64.dll` proxy mapped beside it (this copy stays passive; a mutant that does not look must fail) and the export table; `tools/qa_dropin.sh` (one game slot)
checks the chain load and the passive copy in the real game, each with its mutant; `make -C common test` tests the check on synthetic exes, truncated
files and the real exe; `tools/unknown_build_slot.sh` (one game slot) starts a slot copy whose exe has one code byte changed and requires: the game keeps running, exactly one message box, nothing hooked or started (and a proxy that ignores the check must hook it); `tools/buildid_of.py <exe> --check` verifies the constants against a game exe when the target build changes.

### Making a release

`tools/release.sh v0.x.y` builds, in `local/release/`: `witness-coop-v0.x.y.zip` (+ `.sha256`; proxy, launcher, model, thin Setup, scripts, `install-linux.sh`,
`README.txt` with the version, `LICENSE.txt`) and `witness-coop-v0.x.y-single/Witness-Coop-Setup.exe` (+ `.sha256`), the main download.
`tools/check_release_package.py <zip> --single <exe>` checks the inventory, the checksum, that the zip holds the proxy, and that the one-file setup carries the same
bytes as the zip under its own compiled-in checksum. **Signing:** set `SIGN_CMD` (and optionally `SIGN_VERIFY_CMD`) and every `.exe` and `.dll` is signed before it is hashed and
embedded, the one-file setup last, then verified; without it the build is unsigned and says so. See [docs/SIGNING.md](docs/SIGNING.md) (certificate, `SIGN_CMD` examples,
Microsoft's false-positive submission). `tools/qa_installer.sh <folder of the supported build>` runs everything game-free: the release build, the PowerShell installer suite on disposable
fixtures (install, update, repair, refusals in plain words, uninstall, the v0.1.0-alpha layout migrated, interrupted and resumed, another mod's `dsound.dll`
chained), the one-file setup (hash, planted links, exclusive folder, zip location, a real run), `install-linux.sh` (with fixture Proton prefixes), and the mutants (an unknown exe
installing, an unchecked payload hash, a followed link, a reused folder, a missing zip message, a file overwritten without a backup, an uninstall removing an unowned file, the
game's Ansel DLL put back unverified, the old proxy left behind, the chain file ignored, the Proton override written for the wrong exe or left behind on uninstall) that must fail. `tools/test_release_signing.sh` tests the signing hook with a stand-in signer. Publishing is `gh release create v0.x.y --prerelease <the Setup.exe, the zip and their .sha256>`
(the notes go in `docs/RELEASE-NOTES-v0.x.y.md`). The launcher's updater only accepts a release with the exact asset names and tag shape in [docs/UPDATER.md](docs/UPDATER.md).

## Try it (two copies on one PC)

Requirements: Windows with the Steam build above; WSL2 with `make` and MinGW-w64 (`x86_64-w64-mingw32-g++`); Windows Python 3
(default `python.exe`, override with `PYW`); Python 3 in WSL. Nothing here installs anything for you.

**Test copies.** Optional machine configuration belongs in ignored `local/env.sh`.
Set `PYW` to Windows Python. For concurrent QA, configure `WITNESS_RESOURCES` to
an external provider implementing the interface in [docs/RESOURCES.md](docs/RESOURCES.md).
It supplies isolated copies, ports, process registration and cleanup. The public
repository has no required workstation service or private checkout path.

The proxy goes into two disposable copies of the install, never the real one:

```
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-1
cp -a "<Steam library>/steamapps/common/The Witness" local/witness-2     # (then set WITNESS_DIR1/2, configure an external resource provider)
tools/install.sh       # builds the DLL, checks both exe hashes, installs the proxy as dsound.dll (a copy in the old layout gets the game's AnselSDK64.dll back)
tools/restart.sh       # starts both with -skip_config_dialog -windowed -new_game, side by side
tools/restart.sh B     # restart only B with a fresh world, keeping A and its progress running (also accepts A)
```

Each copy is 3.9 GB. **The game keeps its saves in `%APPDATA%\The Witness` whatever folder the exe runs from**, so a copy does not
isolate them: starting a New Game from a copy rewrote an existing save there. Back that folder up first.
A hand test: walk player A (left window) to the first panel at the end of the start tunnel and solve it; the door opens in B
as well (B's `local/witness-2/witness_proxy.log` says `remote panel 0x64 applied as solved`, `remote door 0x34eda opened`).

## Experimental groups (2–16 players)

The `coop16` branch adds a configured full mesh of up to 16 players: independent reliable streams and ACKs,
all remote poses and avatars, player selection on the map, and shared panel, door, panel-power and cable state.
Group cable mutations carry logical versions so concurrent changes and old snapshots converge consistently.
Detection follows the requested power level while the cable glow fades; a genuine local reversal is still sent during
a remote echo-guard window. New commands are detected before restart snapshots are built.
The two-player `peer=<port>` configuration uses protocol v6. Group mode uses distinct v7 packet tags; every member must
use the updated DLL and list every other member, with distinct player IDs and UDP endpoints.

Generate configurations from a JSON roster, for example:

```json
[{"id":1,"host":"127.0.0.1","port":48001},
 {"id":2,"host":"127.0.0.1","port":48002},
 {"id":3,"host":"127.0.0.1","port":48003}]
```

```sh
python3 tools/group_config.py roster.json local/group-configs
```

Copy each generated `player-<id>.cfg` to `witness_coop.cfg` beside that player's DLL in a disposable game copy.
Each config has `id`, `listen`, and one `peer=<id>,<IPv4 address>,<port>` line per other member (15 lines for a
16-player group). Keep `save_dir=instance` to isolate saves. Launch and install the copies as described above;
`tools/restart.sh` generates two-player configs, so it does not preserve group configs.
Group partner video displays the lowest-ID remote player's stream. Avatar choice is still local.

Validation so far: the eight-player game-free test covers lossy links, reordering, duplicates, independent ACKs,
a temporarily unreachable receiver, progression from every player, all remote poses, cable conflicts and a peer restart.
Five group mutants must fail, including recovery after 4,200 changes while one receiver is unreachable. `python3 tools/test_group_config.py` checks the 16-entry roster capacity and invalid rosters.
The production Windows UDP layer also passed an eight-client fake-world integration test with 25% loss,
cable on/off and a restarted client (`make -C proxy/core transport-test`; no game or resource provider slots).
A two-real-game group run passed a normal input-driven bot solve, door sync, progress originating on both clients,
pose/avatar submission, packet loss, restart catch-up, empty queues and byte-identical real saves.
`tools/qa_group_render.sh` also passed with one real game and seven lightweight protocol clients: seven actual D3D
avatar submissions with distinct positions, one moving gait and six stationary gaits, bidirectional panel progression,
a normal local bot solve and drained queues under 25% packet loss. Its shared-avatar-state mutant was caught.
This uses one game slot; it does not count as eight real game processes. The `modelstate` command writes the latest
render frame's submitted IDs, positions and gait speeds to `witness_models.json` for these checks.
Eight-real-game QA is **pending** while other resource provider tests occupy the available RAM: `tools/qa_group.sh 8` first checks resource provider activity and RAM, then leases only this
worktree's slots, runs a normal bot solve, checks every world's process memory, originates progress on every client,
and verifies restart recovery and drained queues. It refuses insufficient RAM rather than displacing another test. Groups of three or more also defer while another sweep or suite is live, so a client restart cannot make occupied test capacity look temporarily free.
Sixteen live clients and remote-PC networking have not been tested.

## Reporting a problem

Open a [bug report](https://github.com/TommoDotCommo/witness-coop/issues/new?template=bug.yml); the form asks for what is needed. A report
from another PC is only useful with its logs, so:

1. Press **F2** the moment something goes wrong (any player). It saves the last minute of that game (frame rate, where everyone was,
   round trip and lost packets to each player, the sync queue) next to its log, and sends the marker to the other players, so every
   machine saves the same moment under the same number. A toast says "Marker N saved". If a custom binding already uses F2 the marker is on F12
   (the F1 guide names it).
2. Open **witness_launcher.exe** and press **Send diagnostics** (About tab; it works after a crash too). A few seconds later a zip,
   `witness-coop-diag_<date>_<id>.zip`, appears on the desktop with Explorer pointing at it, and the launcher says what is in it:
   the logs of recent game and launcher runs, flight recordings, a crash dump if the game crashed, `witness_coop.cfg`,
   `witness_proxy.log` (the last run), and a summary of the PC (mod version, the game's build hash, Windows version, graphics card
   and driver, network adapters, firewall rules, ping to the peer). Your Windows and computer names, profile folder, IP addresses (the same
   address always gets the same number: `ip-3`), MAC addresses and Steam IDs are replaced before it is zipped. A crash dump is a piece of the
   game's memory: the same strings are overwritten in it, but it may hold more than that. No saves, no game files, nothing uploaded: you
   attach the zip to the report yourself. Attach one from every player if you can, the host's first.
3. Say which player you were, the mod version, how you connected and where on the island you were.

Every run (the game and the launcher) writes its own log, `%LOCALAPPDATA%\witness-coop\logs\<UTC start>_p<pid>.log` (the launcher's has
`_launcher_` in the name). Nothing is overwritten by playing again: the newest 100 MB are kept (`max_log_mb=` in `witness_coop.cfg`), and a run
that crashed is never deleted. `witness_proxy.log` (in `%LOCALAPPDATA%\witness-coop\`, or beside the DLL for a per-instance copy) is still the last run only, and the one before it is `witness_proxy.previous.log`.
Each log line is numbered and hash-chained, so a missing or edited line shows (accidents, not forgery).

**Crashes.** The mod installs an unhandled-exception filter (not a first-chance handler, so exceptions the game catches itself, as
`IsBadReadPtr` does all the time, produce no marker and no dump). On a crash it writes the crash lines (faulting module and offset, registers, the
code addresses found on the stack) to the log, `<session>.crash`, the flight recording and `<session>.dmp`, a small minidump that also holds the
mod's data. It cannot see `__fastfail` or a killed process: such a session simply has no "session end: clean exit" line. Developers read a
submitted zip with `python3 tools/diag_report.py a.zip b.zip [--map proxy/dsound.map --dll proxy/dsound.dll]`: it checks the manifest and the
hash chains, lists the sessions, crashes (a proxy crash is mapped to the object file and function through the linker map `make -C proxy`
writes; keep the map and DLL of every release), desyncs and markers, summarises the flight recordings, and with two zips lines up the
shared marker (`--timeline merged.txt --align`). Game-free tests: `python3 -B tools/test_diag.py` (anonymisation with mutants: a computer name, a
user name, a username path, an IP address, a Steam ID and the dump masking, each left in, must fail), `make -C launcher test` (log chain, pruning, crash
dump of a child process, with mutants). Test-only settings in `witness_coop.cfg`: `diag_dir=instance` (logs in `witness-coop-logs` beside the DLL, for test copies),
`test_crash=1` (the command `testcrash` really crashes the game; `testhandled` raises a handled exception), `crash_handler=0` (mutant: the handler does nothing).

## How it works

The exe imports `DSOUND.dll` (DirectSoundEnumerateA and DirectSoundCreate8, by ordinal) and the game folder does not ship it, so Windows loads
`dsound.dll` from the game folder first. `proxy/` builds that file: its twelve exports carry the system `dsound.dll`'s names and ordinals and are stubs that
load the system copy by full path (`GetSystemDirectory`, never the bare name, which is the proxy itself) on first use and jump to the real function; with no
system copy every call returns a failure instead of crashing. (Until v0.1.0-alpha the proxy replaced the game's `AnselSDK64.dll`, which no mod manager can
install and which Steam restores on every update.) `winmm.dll`, the other candidate, is ruled out: the mod's voice uses `waveIn` from it. `DllMain` only starts
threads; the chain file and the system copy are loaded from them.

| Part | File | What it does |
|---|---|---|
| Transport | `core/peers.h`, `steam_net.cpp` | The roster and whom a datagram is taken from (UDP endpoint or SteamID, game-free, `tests/peers.cpp`); the Steam transport over the game's own Steam connection ([docs/STEAM.md](docs/STEAM.md)) |
| Network | `coop.cpp` | Protocol v6 UDP: pose at 30 Hz (`WTP6`, 40 bytes), reliable events (`WTE6`, 32 bytes: epoch, sequence, kind with bit 8 set for snapshot events, entity), cumulative acks (`WTA6`, 24 bytes); lengths include checksums. A peer session change starts a new epoch and full state resend |
| Overlay | `overlay.cpp` | Patches slot 8 (`Present`) of the swap-chain vtable (found through a dummy device), draws the player model (`model_gpu.cpp`, `model/`) or the flat puppet; also runs queued game commands and remote events between frames |
| Model | `model/model.{h,cpp}`, `model_gpu.cpp`, `depth.cpp` | `model/` is game-free C++ (glTF loading with cgltf and stb_image, skeletal animation, CPU skinning, camera maths, the remote player's walk state) with its own T0 test; `model_gpu.cpp` is the D3D11 skinned-mesh renderer with a GPU occlusion query; `depth.cpp` patches the context vtable (`OMSetRenderTargets`) to find the game's scene depth buffer |
| State | `state.cpp` | Detector thread (10 Hz) and the applier; echo guard and stable-world gate |
| Input | `input.cpp`, `game.cpp` | Injects keys, mouse and clicks into the game's own input pump, teleports the player, takes screenshots, parses `witness_cmd.txt` |
| Hooks | `hooks.cpp`, `config.cpp` | Import-table hooks installed from `DllMain` (memory patching only): `SHGetFolderPathW` (save folder), `CreateFileA/W` (`data-pc.zip` share mode); settings read lazily from the cfg |
| Game calls | `game.cpp` | Door opening at `base + 0x8E4B0`; panel power-on at `base + 0xC2890`, both type-checked and called on the render thread; `witness_cmd.txt` for development |

Build 11370412 facts the code relies on (module-relative): entity table `[[base+0x62D0A0]+0x18]` indexed by the mod's entity IDs
(table indices, including zero); entity
`+0x08` points to a type descriptor (`Machine_Panel` `0x62E670`, `Door` `0x62DE70`); camera position `0x6304C0`, angles `0x6303EC`;
the world projection's scale terms are the floats at `0x649910` (x, negative) and `0x649924` (y): `cot(half vertical fov)` and that divided by the aspect, rebuilt every frame from the saved option `user_fov_vertical` (float at `0x6208BC`, default 54 degrees); the overlay reads them (`proxy/gamecam.cpp`) so its lines, puppets and ink follow the player's field of view and window shape (`old_projection=1` restores the old fixed 60 degrees, a test twin);
a panel is solved when the float at `+0x294` is `1.0` and the int at `+0x298` is `1` (a solve also sets `+0x278`, `+0x280`, `+0x290`);
a door is open when the word at `+0x1D0` is `1.0`. The game's stored target IDs use a separate namespace: subtract the table
base ID at `[[base+0x62D0A0]+0x08]` (observed `1`) to get a table index. Writing those fields into another game does not start a door: its opening is
started by the function call. Panel requested power is at `+0x2A4`; the visible level at `+0x2A0` follows it in the engine. Sources and the evidence for each are in the hub notes.

`witness_coop.cfg` (in `%LOCALAPPDATA%\witness-coop\`; beside the DLL as a per-instance override when it has an instance key, see above): `id`, `listen`, `peer` (UDP ports on localhost), optional `peer_host`. Instance isolation (all
off by default, so a player's own game is unchanged): `save_dir=instance` (the game's save folder, `%APPDATA%\The Witness`, becomes
`<game folder>\appdata`, or give a path; the owner's real saves are then never touched), `cap_fps=N` (frame cap), `share_zip=1`
(see above), `force_active=1` (the game believes its window is always active: full speed, and it accepts injected input, without taking the focus or the mouse; the real cursor is replaced by a virtual one). Test-only switches: `drop_first_event=1`, `no_resend=1`, `no_echo_guard=1`, `no_resync_on_restart=1`, `skip_entity_zero=1`, `no_panel_power=1`, `no_cable_sync=1`, `no_cable_off=1`, `no_off_snapshot=1`, `no_snap_reassert=1`, `no_cable_arbiter=1`, `no_power_link=1`, `no_target_dispatch=1`. The log is `witness_proxy.log` next to the DLL.

Keys added with the lobby and the race (all optional): `game_mode=race` (nothing of the world is shared, see features.md) with `race_goal=panel:<hex>|count:<n>` and `race_countdown_ms=` (default 5000), `voice_channel=proximity|radio|global|direct:<player>` and `key_voice_channel=` (default `Y`), `see_through=0|1` and `ghost_alpha=`, and for the launcher `lobby_port=` and `lobby_advertise=`; for Steam, `peer=<id>,steam:<steamid64>` and `transport=steam` (written by the launcher, [docs/STEAM.md](docs/STEAM.md)); test switches `race_emits=1`, `race_applies=1`, `no_race_hud=1`, `no_voice_channel=1`, `no_voice_channel_key=1`, `voice_no_self=1`. `racestat`, `ghoststat` (in `modelstat`), the channel in `voicestat` and `netstat`/`steamstat` (the transport and each Steam session) are the readouts the tests use.
Optional back TV settings: `pov_tv=0|1` (enable viewing back TV on the nearest player, default 0; hotkey F10), `pov_tv_consent=0|1` (consent to streaming POV snapshots to subscribing viewers, default 0; hotkey F11). Commands `tv 0|1`, `tvconsent 0|1`, and `tvstate` control and inspect TV state. Aim at a nearby player and press J to deploy their stationary audience screen: it goes up 3 m in front of you with its picture facing you, and the three stools stand where you are (turn round to sit). While the mod measures the ground under it (a few seconds: it drops your player onto each point) your picture is held still and the others keep seeing you where you stood (`tv_probe_show=1` is the test mutant that shows the jumps). The picture is 256x144 at 5 frames a second (dithered 8-bit colour, up to about 185 KB/s to each viewer). Press J again to retract it. Aim at a wooden audience stool and press K to sit facing the screen; K or movement stands up. `key_tv_deploy` and `key_tv_seat` are configurable through the common controls table. Use flat, open ground for the stools (they take the wearer's ground height, so on a slope or a cliff edge some of them hover or sink). If two players take the same stool in the same instant, the one with the higher player id stands up again. `tools/qa.sh seats` runs the two-client seat scenarios (simultaneous seating with its mutant, the wearer or the deployer leaving while the other sits, the hidden ground measurement with its mutant); `tools/qa.sh seats terrain` measures the stools against the real ground and fails today on uneven spots.

### Character migration

The proxy always loads `witness_models/adventurer.glb` beside the DLL. Old `model=` (including `none` and external paths), `character=`, `model_by_player=`, `key_character=` and `no_model_select=` settings are ignored. The launcher preserves these unknown lines when saving, but offers no model selector. F6, F7, C and the old `model` / `character` commands no longer select a model. `tools/install.sh` replaces the disposable copy's model directory with the bundled adventurer.

Existing game saves are unchanged. Campaign `.coop.txt` metadata still reads and preserves historical character strings for compatibility; new player entries record `adventurer`. These strings never select an asset.

Fit overrides remain available for diagnostics: `model_height=<m>`, `model_speed=<m/s>` and `model_yaw=<degrees>`. The bundled sidecar sets eye alignment, walk/run speed and the eyelids' closing stretch (`lid_close=`).
Lighting: `model_sun=` (default 0.55), `model_ambient=` (0.30), `model_shadow=` (0.55; 0: no shadow), `model_shading=0` (the old flat shading); `no_model_life=1` turns the life layer off (a test mutant). The other players' footsteps are heard through the voice mixer: `footsteps=0` silences them, `footstep_volume=` (percent) scales them. See [docs/LIFE.md](docs/LIFE.md). Depth settings are `depth_bias=<fraction>` and `depth_near=<m>` (default `0.0597`); model mutants are `no_model_depth=1`, `no_model_gait=1`, and `no_modelstat_queue=1`. While paused the proxy hides its overlays (`no_pause_hide=1` and `pause_wrong_flag=1` are test mutants).

Protocol v6 adds a fixed 12-byte versioned appearance record to presence (36 bytes total). Pose, event, ACK, ping, social, presence and line packet magics changed so older peers are ignored. Group pose/event/ACK use distinct version-7 tags; all clients must update together. The stable-world gate and reliable event semantics are unchanged. The removed model-name field does not return. See [appearance design and verification](docs/APPEARANCE.md).

`tools/showcase.sh [N]` captures N adventurers in a group; the [showcase page](docs/SHOWCASE.md) labels older pictures separately.

Both peers must use the updated DLL. Protocol v2 added panel power-on and v3 adds cable lighting (event kind 4: a cable is lit when `+0xC4` and `+0xC8` are 1.0; applied through the game's own `exe+0x175EE0`, the call its target dispatcher at `exe+0xC24F0` makes for a cable target); each version uses new packet magics, so older peers are ignored.
Protocol v4 introduced cable power-off (event kind 5, the game's own `exe+0x175E50`, which sets the requested level `+0xC8` to 0 and lets the glow fade). A cable that was lit and goes dark is announced; a snapshot also lists default-lit cables that were switched off, so a fresh peer learns them; a peer's snapshot never relights a cable this player switched off (this player answers with the off state); and when both players switch the same cable at the same moment the lower player id wins, so both worlds end the same.

Progress that undoes itself is shared too (event kinds 6 and 7). A timer panel that resets (the Shadows door timers `0x334DB`/`0x334DC`: about 20 s after the solve the game sets `+0x278`, `+0x280`, `+0x290` and `+0x294` back to 0 and leaves `+0x298`) is announced as un-solved, and the other game writes the same fields; before this the other game kept them solved for good. A door that shuts is announced as closed, and the other game calls the same door function with a target of 0, unless its door is already on its way shut (`+0x1D4`, where the door is going, is 0: the timed door `0x19B24` shuts by itself in both games). A save loaded again is still told apart by the world's generation, so a load never goes out as a run of un-solves. A load takes the world down before it builds the saved one, and in between the generation cannot be read (the entities it is made of are missing): while it cannot, nothing is detected, and when the world reads again a new generation is the load (the same one was a moment's unreadable entity, which also happens in a game that loads nothing, and detection goes on). Before 2026-11-02 the detector ran on in that gap, so in about one in four 'save point restore' runs a game either took a silent new baseline when its 3 s entity-table rescan fell there, keeping the progress of a save point restored into a fresh game to itself, or sent the half-built world's default-open doors as live progress. T0: the load-gap cases in `proxy/core/tests/reload_sync.cpp`, twins `reload_gap_ignored` (also the live mutant 'save point restore with the load gap ignored') and `gap_is_load`. T0: `proxy/core/tests/unsolve_sync.cpp` (lost first send, refused call, no echo, the timed door shutting in both, a reload) with the twins `no_unsolve_sync`, `no_door_close_sync`, `no_apply_retry` and `no_reload_sync`. Checked in the game on 2026-10-30: the timer panel solved on A reset on A and on B, and the door shut on both. A door that shuts and opens again within about 100 ms (seen on `0x14724` and a hedge maze shortcut) sends both events in one round; the open is deduplicated by where the door is going (`+0x1D4`), not where it stands, since the close just applied leaves it standing open until the next frame (before, the open was dropped and the door stayed shut in that game). T0: the flicker case in `unsolve_sync.cpp`, twin `open_dedupe_by_position` (a game call cannot make that dip: shut and opened again 5-120 ms later, a door goes on shutting and takes about 20 s to come back).
Applying a remote panel solve also does what the game's target dispatcher (`exe+0xC24F0`) does for targets that have no event of their own (lasers, pressure plates, gauges and the like), and a panel powered by a remote event gets its source back-link at `+0x2C8` like a native solve.

Without `force_active` the game runs unthrottled only while its window is the active one (otherwise about 10 fps) and ignores keys and
mouse buttons. It also takes the foreground by itself while loading (`tools/restart.sh` runs a guard that hands the focus back).

**In-process control** (what the bots use; no window needs the focus and the desktop is never touched). The proxy polls
`witness_cmd.txt` (next to the DLL, one command per line, the whole file is taken at once, one input event is executed per frame):

| Command | Effect |
|---|---|
| `key W|A|S|D|SPACE|ENTER|ESC down|up` | a key press as the game's own key messages (holding `W` for 3 s walks the player about 9 m) |
| `mouse <dx> <dy>` | relative mouse motion as raw input (0.0025 rad per count when looking; 3.6 counts per pixel on a puzzle) |
| `button L|R down|up` | a mouse click as `WM_LBUTTONDOWN`-style messages at the window centre (the virtual cursor) |
| `tp <x> <y> <z> <yaw> <pitch>` | put the player there and face that way (the camera is 1.69 above the player entity) |
| `shot <name>` | back buffer to `shots\<name>.bmp`, also while the window is unfocused or covered (`tools/bmp2png.py` converts) |
| `door <hexid>` | open that Door entity with the game's own function |
| `stats` | log the input counters |
| `modelstat` | log the model's state: pixels that passed the depth test (GPU occlusion query), animation clip and time, measured speed, pose |
| `tvstate` | dump POV TV state JSON (`witness_tv.json`): enabled, consent, subscribers, frame hash, sway, visible pixels |
| `fakepose <x> <y> <z> <yaw>` / `fakepose off` | test hook: draw the peer at this eye position instead of where it is (a player cannot be teleported behind a closed door, but the depth test must be checked there) |
| `probe` / `depthview on|off` | diagnostics: log the game's depth at a few pixels; draw the depth buffer instead of the model |

## Testing every puzzle, or one puzzle

The island has 594 placed panels. `tools/sweep.sh` (resource provider mode: leases two slots, starts both games) and `tools/sweep.py run` teleport to each one with the
dev menu, draw a solution through the in-process mouse and take the game's own solved flag as the verdict. Nothing is marked solved by a dev action.
`tools/puzzle.sh` does the same for one puzzle, for a tester: `puzzle.sh list [text]` (size, rules, solver status), `show ID`, `go ID [A|B]` (teleport,
then play it yourself), `solve ID [A|B]` (teleport and draw the solution). The ids are the hex ids the dev menu shows.

Where the line comes from, in this order: the game's stored solution (97 panels carry one from the first frame: the plain-looking grids whose rule is
not in the cell decorations, and a few more), a plain path (mazes, lines), or `tools/psolve` (game-free, C++, T0 tests with a mutant per rule:
`make -C tools/psolve test`): dots (black, blue, yellow), gaps, squares, stars, triangles, polyominoes with rotation and negatives, erasers (also on
dots) and symmetry, on grids. A panel no solver covers yet is reported as `no solver` with the reason, never forced.

Four panels in the Keep are floor puzzles: their line is drawn by walking over pressure plates (one plate per node and per edge of the graph; an edge without a plate cannot be walked), so `tools/win/wwalk.py` holds the player over the plates of the solution in order instead of drawing with the mouse. `sweep.py` and `puzzle.sh solve` choose by themselves (`tools/win/wplates.py` lists which panels have plates). A panel is only started once the game has powered it (`+0x2a4` is 1.0): solving the panel upstream powers it, so the sweep goes in passes and waits for power. `sweep.py gating <graph.json>` says what powers the panels it could not try, `sweep.py report` summarizes, `sweep.py export <file>` writes the results as json.

The bot (`tools/win/wdraw.py`, header has the measured details): aims the camera at the panel, enters it, finds the start circle by projecting the
panel's nodes with a pinhole camera (its size in metres is scanned), and walks the line edge by edge closing the loop on the panel's traced-node count.
Results of a run: `<run folder>\sweep_results.<shard>.json` (one record per panel: `solved`, `failed` with the reason, `no solver`); the master list of
panels (taken from a fresh game, because it holds the stored solutions) is `<run folder>\panels.json`. Only the first call of `sweep.py` needs a fresh game.

## Playing the game through with N bots

`tools/playthrough.sh N [--goal credits|all] [--minutes M] [--fps F] [--drop PCT]` plays the game in its own order with N real games in
one co-op world (one game alone for N=1), and records what happened. The order comes from Archipelago's vanilla logic for the game
(`tools/playthrough/data/WitnessLogicVanilla.txt`, MIT, source in `data/SOURCE.md`): every region, panel, door and laser, and what each needs.
Its 584 panel ids are the game's own entity ids. `tools/playthrough/logic.py` derives the open doors, lit lasers and reachable regions
from the solved panels. `planner.py` hands each free bot a different panel that is available. Panels that open the most new work go first,
so N bots open the world N ways at once. The credits route is 238 panels (pruned from 584 and cached in `data/credits_route.json`).
`python3 tools/playthrough/test_playthrough.py` and `test_variants.py` are the game-free tests, with 9 mutants.

A bot moves only to a panel the logic says is reachable and the game says is powered (the dev menu's teleport), draws the solution with
in-process input (`tools/win/wdraw.py`, `wwalk.py` for the floor plates), and takes the game's own solved flag as the verdict.
Some panels have two exits or two starts and only one of them powers what the panel is cabled to (the tutorial gate's latch needs Back Left's
other exit). For those, `solver.variants()` lists one solution per start/end pair. When a line gets stuck on a panel whose prerequisite could
have been solved another way, the bot redraws the prerequisite the next way (`WDRAW_REDRAW=1`) and tries again.

Panels the bot cannot do are taught by a person. `tools/record.sh` starts one game from a save on the first monitor. It puts the player
1.4 m in front of each panel in turn, with a message on the screen, and records what they do (`tools/win/wrecord.py`). The recording keeps
the pose, the panel's state and the traced line twenty times a second. `python3 tools/playthrough/teach.py` turns the recordings into
`tools/playthrough/data/taught.json`: per panel, the line the game accepted, each node's world position (from the traced-edge records),
and where the person stood. For a taught panel, `run.py` draws that line instead of the solver's. `wdraw.py` (`WDRAW_TAUGHT`) stands there
and looks at the start circle. After entering the puzzle (which turns the camera to the panel), it projects the taught positions from the
new view and clicks the start, with no size search. `python3 tools/playthrough/test_teach.py` is game-free, with 3 mutants.

Co-op is checked on every solve: the panel must turn solved in every other world, and the time each peer took is recorded. The worlds are
compared every few rounds; a difference still there 5 s later is a persistent divergence, otherwise in flight (a door moving).
`python3 tools/playthrough/divergence.py <run folder>` checks a run: a persistent divergence that a later comparison no longer shows has
resolved (a slow solve), one on `tools/playthrough/data/divergence_allow.json` is known (keyed by entity id, with the reason: the Shadows
timer panels and their timed door), any other fails; `summary.json` carries the result and `report.py` prints it. `test_divergence.py` runs it
on three recorded runs with injected mutants. `python3 tools/playthrough/proplog.py <each game's witness_events.log>` gives the exact
propagation times from the games' own logs and says why a slow one was slow (`test_proplog.py`). The early 4-bot runs' p95 of 12-13 s
(max 36 s) was a receiver re-gating after it took a timer panel or a timed door for a save load (every arrival over 2 s of 413); since the
world's generation decides what a load is, p95 is 0.25 s in the core's lossy-link test (`proxy/core/tests/propagation_latency.cpp`).
Output in `<run folder>/playthrough/<start>/`: `events.jsonl` (solves with the time split, failures with the drawing trace and a shot,
propagation, divergences, redraws, blockers) and `summary.json`. When nothing is left that a bot can do, the run names the panels that block the goal.

## Automated checks

**T0, no game needed:** the sync logic lives in `proxy/core/` (`sync_core.{h,cpp}`, pure C++17, talks to the game only through a `World` interface; `state.cpp` is the game adapter). `make -C proxy/core test` (needs a clone of the Co-op Anything hub for its fuzzer; `HUB=<dir>`, defaults to a sibling `coop-anything` checkout; override with `HUB`) fuzzes two cores over a lossy link with a fake world: 500 sessions must pass, and each of 10 bug twins (no resend, no echo guard, no dedupe, ...) must be caught. About 3 seconds.

The same command also runs deterministic application regressions: acknowledged events survive a missing panel, a missing door,
or repeated refused door calls, then apply without another network packet and without echoing. An unrelated door must still open,
wrong entity types must be rejected, and each retry case must catch the `noApplyRetry` bug twin. These simulate adapter failures;
they do not demonstrate a late-loading entity or a refused call in the real game.

Power-on regressions also run in T0: dropped first sends, refused power-on calls, no echo, keeping the target unsolved,
and restoring power to a restarted receiver after the original events were acknowledged. Disabled power sync, no retry,
no echo guard and no restart resync each have a twin the regression must catch.

**Model code, no game needed:** `make -C proxy/model test` (also run by `make -C proxy/core test`) loads the bundled adventurer and checks skinning against the bind pose, that the walk loops, quaternion interpolation, that the camera matrix agrees with the projection the puppet always used, the placement of the figure (feet, height, facing, left/right), the walk state (speed, idle, teleports, clip choice by name) and that the normal and material maps load; 15 mutants cover matrix order, looping, quaternion normalisation, placement, gait, blending, run timing, joint limits, eye fitting, skins, flat colours and emote matching. The life test (`life_test.cpp`, 14 mutants) covers the blink, the glance, the view pitch, breathing, the cloth pendulums, the coat's skirt clearing the legs through the walk, the fidget scheduler, the brows' acting with emotes, the mouth moving with speech, the arm reaching for a panel being traced, and the body's turn lag, sideways half-turn and reverse walk ([docs/LIFE.md](docs/LIFE.md)). `proxy/core/tests/run_protocol.py` additionally checks protocol migration with three mutants.

**T2, two real games:**

`tools/qa.sh` restarts both games for each scenario, and a bot plays player A through the proxy's in-process control (teleports to the
first panel, aims, clicks to enter the puzzle, traces the line, clicks to finish), with no window focused and no desktop input, and `tools/state_diff.py` compares the
solved panels and open doors read from both games' memory. Scenarios marked MUTANT are deliberately broken and must fail.
With no arguments (or `--all [filter ...]`) it runs the whole suite in parallel shards through `tools/qa_par.sh`, one resource owner and
two slots per shard, as many shards as the provider has room for (2 free slots and about 4 GB of free RAM each above the provider's own RAM floor, started 90 s apart, at most `QA_SHARDS_MAX`,
default 4; it waits up to `QA_WAIT` seconds for room instead of failing). The shards take scenarios from one queue, longest first by the
typical seconds in `tools/qa_durations.json` (`tools/shard.py`, tested game-free by `tools/test_shard.py`), and the run ends with a table of
scenario, shard, seconds and result and the total wall time. `tools/qa.sh --serial` runs the suite on one pair of games.

`tools/qa.sh model` runs the adventurer suite (`tools/suite_model.py`, `tools/win/wmodel.py`): visibility, idle/walk/run animation, measured speed, render-thread statistics, occlusion behind the first door, blinking, the glance at a near player and the sun shadow on the floor. It also checks migration from a removed model name. Mutants disable gait, read statistics on the command thread, draw through walls, turn the life layer off and turn the shadow off. The updated live scenarios require a fresh verifier run; no old model-selector results apply to this version.

Last full run from this repo (2026-10-22, one machine, localhost, build 11370412, about 8 minutes now that the bot teleports instead of
walking 46 m). Every scenario also checks that the owner's real save folder is byte-identical, that the instance's saves were
redirected, that the real mouse is not grabbed (system clip rectangle is the whole screen) and that the frame cap holds:

```
baseline                                 PASS   identical (1 panel, 10 doors); B sent nothing back; no resend needed
lost first send of every event           PASS   identical; A resent all 11 events
MUTANT: no resend + lost first send      PASS   games diverged (panel 0x64 and door 0x34eda only on A), as required
MUTANT: frame cap off                    PASS   game A read 1123 fps (uncapped), as required
MUTANT: echo guard off                   PASS   B echoed its 2 events back, as required
ALL OK
```

Two Witness agents (separate worktrees, so separate slots, ports and folders) ran the baseline **at the same time** beside an MDK2
agent's own QA (34 of 34): all passed, and a monitor sampling the foreground window every 0.2 s (1,648 samples) never saw a game own it.
An earlier run of the in-process suite passed while the bot had done nothing (two untouched games are "identical"); the suite now
requires the bot to report the panel solved.

The suite found one real bug in an earlier run: a default-open door (`0x338d5`) powered up on player B a few seconds *after* the
"world stable" gate, so B sent an event with nobody having solved anything and the baseline check ("B sends nothing") would have
failed. The gate now also needs the same set of solved panels and open doors on three scans, and changes during the first 15 s after
the gate are folded into the baseline (the log says `changed during start-up, folded into the baseline`). It happened once in about nine
starts and did not recur in the final run, so the fix is in place but not shown to trigger.

That window must not swallow what the game does in response to progress the mod applies itself. A save point restore loads the game
save and then applies the progress list; with the list landing just as the reloaded world passed the gate, its panels reached the other
game with the full state, but the doors, panel power and cables the game's own scripts turned on in response came 1 to 12 s later, inside
the window, and were folded: in the restore without the save file (2026-11-02, 3 of 3 runs) 8 doors, 3 powered panels and 6 lit cables
stayed on A only, and a preset applied as soon as a fresh game was stable lost doors `0x4CA3` and `0xA170` on B (the solves were
detected live, but those doors open more than 5 s after them, past the rule that shares a solve's cascade). Everything that changes progress by itself (the solve queue of a progress list, preset or `complete`, the dev menu's
actions, the doors opened by hand) now calls `SyncCore::modProgressApplied`, which ends the start-up window (or skips it, with a full
state for the peer, when it came before the world was stable); a start where the mod applies nothing still folds its start-up flips.
T0: the list-after-load cases in `proxy/core/tests/reload_sync.cpp`, twin `no_mod_apply_share` (also the live mutants 'save point
restore without the save file, reaction not shared' and 'MUTANT: preset apply during start-up, reaction not shared', which must leave
some of it on A only); the restore without the save file must now be identical apart from panel `0x64`, and 'preset apply during
start-up' identical. A save point restored with its game save is unaffected: the save already holds the list's panels, so the list
applies nothing (`applying a progress list: 0 panels`).

`tools/qa.sh 'restart catch-up'` runs two additional scenarios: B is restarted into a fresh new game after both copies have the
first panel solved and A's original events are acknowledged. The survivor must retain its PID and progress; the new process
must recover all solved panels and opened doors with no echo. The mutant disables the survivor's full-state resend and must
leave the first panel and its door missing on B. Assertions read both processes' state and require a complete entity census.
Targeted run on 2026-10-22: both scenarios PASS; clean state identical (1 panel, 10 doors), mutant missing exactly panel
`0x64` and door `0x34eda`. Real saves unchanged, instance save redirect and cursor checks passed, and A ran at 30.0 fps.

`tools/qa.sh 'second tutorial door'` runs the first-panel bot then `wbot2.py`, which naturally solves Tutorial Bend (`0x182`)
by tracing up and left. Both games must contain both solved panels and open doors `0x34eda` and `0x0`; the mutant skips entity
zero during A's scan and must leave exactly that door missing on B. This caught a real adapter bug: scanning from ID 1
excluded the second tutorial door even though table index zero is valid. The second bot uses a calibrated standing point and
cursor path for the resource provider's 1920x1080 back buffer; other resolutions are untested. `tools/win/wpanel.py <pid> <hex id>` reads a
panel's graph for extending the bot to more puzzles.
Targeted run on 2026-10-22: clean and mutant both PASS (command exit 0). Clean has 2 solved panels and 11 open doors on both
sides; mutant lacks only door `0x0` on B. Save isolation, free cursor and 30.0 fps checks passed. The clean check failed before
the scanner fix, with that same missing door.

`tools/qa.sh 'panel power chain'` solves black/white tutorial panel `0x18af`, which powers panel `0x1b`. The target must
become powered in both games while remaining unsolved. The clean scenario then restarts only B and checks solved panels,
doors and powered panels again after catch-up. The mutant disables A's power events and must leave only `0x1b` unpowered
on B. `state_diff.py` now compares powered panels as well as solved panels and open doors; a panel counts as powered only
when both requested power and visible level are 1. The bot is calibrated for a 1920x1080 back buffer.
Targeted run on 2026-10-22: both scenarios PASS (exit 0). Clean, including fresh-B catch-up, matched 2 solved panels,
10 open doors and 238 powered panels; the mutant left only `0x1b` unpowered on B (237 there). Both kept the target unsolved.
Save hashes, instance saves, free cursor and 30.0 fps checks passed.

`tools/qa.sh 'cable chain'` solves maze `0x293`, whose lit cable powers maze `0x295` in the same garden, then solves `0x295` and lights its cable; both cables, both panels and the powered panels must match on B, a fresh B must recover them, and the mutant without cable sync leaves both cables dark on B. That is a second cable-driving panel (the chain scenario also covers the single maze `0x293`, whose own scenario was folded into it; no walking bot was needed: its neighbours are reachable by teleport; panels such as `0x3505` and `0x290` are not, a teleport to them is refused and the player returns to the spawn point).

`tools/qa.sh 'bot-free effects'` needs no bot. It makes panel `0x32f5` solved on A with the test command `solve` (its fields plus the game's target dispatcher, as a native solve does), so its laser `0x32f9` switches on (`+0x12c` goes from 0 to -1.0). B must end with the same laser state and so must a fresh B; the mutant without the dispatcher call leaves the laser off on B. `tools/qa.sh 'panel power chain'` now also compares the target's source back-link (`+0x2C8`) and has a mutant that leaves it unset. The same scenario switches the default-lit cable `0x18458` off on A with the test command `cableoff`; it must go dark on B and stay dark on a fresh B. One mutant removes the dispatcher call and the power-off sync on B (the laser stays off and the cable stays lit there), another removes the off snapshot on A (a fresh B shows the cable lit again).

**Dev menu, partner view, saves** (`dev_menu=1` in `witness_coop.cfg`; the resource provider tools turn it on; a normal install has it off):
- `F8` opens the dev menu: a map of the island with every panel (green solved, amber powered, grey off), the doors, and both players (you as a cyan pointer, the other player in orange); a list of 31 spots to teleport to; wheel to zoom, right-drag to pan, click a dot to select it. A selected panel shows what it targets, which panels target it, the chain upstream and downstream, and buttons: teleport in front of it, solve it (and run its target), power it, run its target, open its door, light or darken its cable. `F4` opens the nearby-puzzles page (the puzzles within a radius of you, nearest first, same details and buttons). `Tab` switches pages, `Esc` or `F8` closes. While it is open the game gets no keyboard or mouse input (key-ups still pass so nothing sticks). A teleport is checked a moment later and retried at other heights and distances when the game puts the player back at its spawn point.
- `F9` shows the other player's screen in a corner (small, large, off). Each game sends a 480x270 JPEG of its own screen about 5 times a second in UDP chunks (a lost chunk drops that picture); the other game decodes it and draws it. It sees what the other player is looking at or tracing, including panels.
- The Saves page makes a save point (the game's save file, copied after the pause menu opens so the game writes it) and restores one through the pause menu. The game lists saves by the time stored inside them, newest first, so the restore removes the stale copies in the instance save folder and picks the second entry (the first is the save the game just wrote of the running world). The game's save only keeps progress made through its own solve code (each solved panel has a traced solution line), so progress made by the dev tools is kept in a list of solved panels next to the save and put back after the load. Only panels the dev tools solved go in the list: a panel solved by play stays out of it even when "complete N" covers its spot, so the scenario's mutant (no save file copied) really loses panel 0x64. A preset is that list alone: apply it to a new game to start from a given amount of progress ("complete the first N spots" makes one). Loading needs the player to be out of the intro tunnel.
- Test commands in `witness_cmd.txt`: `menu map|nearby|saves|toggle`, `menuat X Y`, `menusel HEXID`, `menudo teleport|solve|power|run|door|cableon|cableoff`, `partner on|large|off|stats`, `partnershot NAME`, `savepoint NAME`, `savepreset NAME`, `restore NAME`, `complete N`. Scenarios: `dev menu` (teleport, input taken while open, solve and cable actions reach B), `partner view` (B's received picture matches A's screenshot: mean difference 5.3 against 83.8 for the control), `preset apply`, `preset apply during start-up` and `save point restore` (a fresh pair restored to 16 solved panels and 18 open doors, with a mutant that skips the save file, which must lack only panel 0x64 and be identical otherwise, and one that lets the detector run on while the load has the world down, `reload_gap_ignored=1`: B must then lack the restored progress; `no_mod_apply_share=1` makes the 'reaction not shared' mutants of the first and the start-up preset).

Not covered by the suite: a real network, more than two players, puzzles beyond the first two tutorial doors, the first black/white tutorial panel and the two chained mazes, the other target types (plates, gauges, bridges, record players: only the laser is checked, and its panel is made solved by command rather than played), the `:slider_cable` cables, a switch-off caused by a real in-game lever (the off call is made by command), loading a different save slot, saves, fullscreen, non-16:9 windows.

The probes in `tools/win/` (`wread.py`, `wsnap.py` + `wdiff.py` for before/after memory diffs, `wnames.py`, `wstatedump.py`, `wfps.py`) are
read-only; `wkeys.py`, `wshot.py`, `wplace.py`, `wfocus.py` and `launch.py` drive the windows.

## Known issues

- **Steam Deck / Linux (Proton) is untested.** The install script, the DLL override it writes into the game's prefix and Play handing over to the launcher are expected to
  work and have not been run on a real Deck or Linux PC. The script itself is tested on fixture folders and fixture prefixes. Overlay under DXVK, voice under Proton and
  window placement are unknown.
- **Another mod named `dsound.dll` is chained for its start-up only.** Its exports get no calls (the game's DirectSound goes to Windows' copy), so a full
  DirectSound replacement does not work together with the co-op mod.
- **Only Steam build 11370412 is supported.** A Steam update, a beta branch or another store's copy turns co-op off with one message (see the FAQ above). Supporting a new
  build means re-deriving the offsets and re-running the checks.
- **The first releases may be unsigned.** Windows SmartScreen and some antivirus programs warn about unsigned programs and about PowerShell started by one. `docs/SIGNING.md`
  is the plan; the build is ready for a certificate.
- The setup window's elevation step (only when the game folder is protected) runs `witness-coop.ps1` from the unpacked folder with administrator rights; a program already
  running as the same user could replace that script in the moment between unpacking and the Windows permission prompt. A fully native setup would remove this.

## License

MIT, see LICENSE.

Handheld radio: hold **R** for session radio, **F1** for configured keys, and use
the hold-**Tab** player panel for voice volume/mute. See [radio controls and verification](docs/radio.md).
