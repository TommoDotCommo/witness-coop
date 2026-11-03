witness-coop @VERSION@ - co-op for The Witness
==============================================

Walk the island together: both of you see each other as the adventurer, every panel
one of you solves is solved for the other, doors open for both, and you can chat, point
things out, draw on the world and talk. Alpha for a first play test: expect rough edges.
Project page and bug reports: https://github.com/TommoDotCommo/witness-coop

You need The Witness on Steam, build 11370412 (the current one). Setup checks the game's
exe and refuses any other build: the mod uses that build's addresses. Every player needs
their own copy and this same mod version (the launcher shows it on its About tab, and offers to update itself when a newer release is out: a banner on Play). If Setup says your build is not supported, see
"Setup says my build is not supported" below.


Install (easiest): the single file
----------------------------------
1. Download Witness-Coop-Setup.exe from the releases page and double-click it. It is one file and
   needs nothing else: do not open it from inside a zip. (If you see "Setup was started from
   inside a zip file", this is the cause; use the single file, or Extract All first.)
2. Setup finds The Witness in your Steam libraries (or asks for the folder), checks the build and
   shows what it will change. Click Install. Windows asks for administrator permission only if
   the game folder is protected.
3. Click Play when it finishes. From now on press Play on The Witness in Steam as usual: the game
   opens the co-op launcher first, and the launcher starts the game for you (alone or with friends).
   Do not want it? Settings > Launcher > "Show the launcher at start" off: Play in Steam then starts
   the game directly; hold Shift while it starts to get the launcher back. witness_launcher.exe in the
   game folder (or Setup's desktop shortcut) opens the launcher too.

The single file unpacks itself into %LOCALAPPDATA%\witness-coop\setup\<version>\, runs the
setup window from there and removes that folder when you close the window.
If Windows shows "Windows protected your PC", click "More info" > "Run anyway" (the first
releases may be unsigned). Setup is a small program that starts Windows PowerShell scripts you
can read: installer-gui.ps1 and witness-coop.ps1 (in this zip; the single file carries the same).


Install from this zip
---------------------
1. Right-click the zip > Extract All, anywhere (not inside the game folder).
2. Double-click Witness-Coop-Setup.exe in the new folder. Steps 2 and 3 above follow.


Install by hand (no Setup)
--------------------------
1. Close the game. In Steam: The Witness > Properties > Installed Files > Browse. That is the game folder
   (it holds witness64_d3d11.exe).
2. Copy these from this zip into the game folder. Nothing of the game is renamed or changed.
     dsound.dll             (the co-op mod; the game loads it from its folder, it passes the sound
                             calls on to Windows' own dsound.dll)
     witness_launcher.exe   (the lobby and settings window)
     witness_models\        (the whole folder: the player model)
   If the game folder already has a dsound.dll (another mod), rename that one to dsound.chain.dll
   first: the co-op mod loads it after itself.
3. Press Play on The Witness in Steam: the game opens the co-op launcher (or start witness_launcher.exe).
Coming from v0.1.0-alpha: delete the old AnselSDK64.dll (the old co-op mod) and rename
AnselSDK64_real.dll back to AnselSDK64.dll (or let Steam's Verify integrity of game files restore it).
Setup does this for you, after checking the file is the game's own.
Uninstall by hand: delete dsound.dll, witness_launcher.exe and the witness_models folder (rename a
dsound.chain.dll back to dsound.dll). witness_coop.cfg and witness_proxy.log may stay.
Setup can later take over a hand install: it backs up what it replaces and can uninstall it.


Steam Deck and Linux (Proton): untested
---------------------------------------
Not tried on a real Steam Deck yet. What should work, from how Proton and Wine load DLLs:
- The game folder is, for example, ~/.local/share/Steam/steamapps/common/The Witness (Desktop mode).
- install-linux.sh in this zip does the hand install above with backups and receipts:
    sh install-linux.sh install              (finds the game; or give the folder as a second argument)
    sh install-linux.sh uninstall
    sh install-linux.sh status
  Open a terminal (Konsole) in the unpacked zip. It refuses any other build of the game in plain words.
- Proton has its own built-in dsound and prefers it, so it must be told to load the mod's.
  install-linux.sh does that for you: it writes "dsound"="native,builtin" for witness64_d3d11.exe into
  the game's Proton prefix (<Steam library>/steamapps/compatdata/210970/pfx/user.reg, with a backup;
  uninstall puts back exactly what was there). Run it with the game closed. The prefix only exists
  after The Witness has run once under Proton: if it is missing, start the game once and run the
  script again, or set this launch option in Steam (The Witness > Properties > Launch options):
    WINEDLLOVERRIDES="dsound=n,b" %command%
- Press Play on The Witness in Steam, Game Mode included: the game opens the co-op launcher inside the
  same Steam launch and Proton prefix, and the launcher starts the game from its lobby. Nothing else to
  add to Steam. On a Steam Deck the launcher fills the screen and works with the controls: stick or
  D-pad to move, A to choose, B to go back, LB/RB for the tabs, Start for the main button.
- Unknown until someone tries: the overlay and model drawing under DXVK, voice chat (microphone) under Proton,
  the game window position on the Deck's screen. Please report what you see (the address is at the end).


Setup says my build is not supported
------------------------------------
The mod only fits Steam build 11370412 of The Witness. Setup tells you what it found (the store if it can
tell, the exe's SHA-256 start), what it needs, and what to do. Usually:
- Steam: right-click The Witness > Properties > Betas > choose None, then Properties > Installed Files >
  Verify integrity of game files. Run Setup again.
- Other stores (GOG, Epic, ...): not supported yet.
- If Steam already has the newest version, the game was updated after this mod: co-op returns when the mod is
  updated. Nothing was changed and the game works as before.
If a game update happens after you installed, the mod notices when the game starts: it leaves the game alone,
shows one message ("Co-op is off") and the game runs as normal. Run Setup again once a new mod version is out.


Playing with a friend
---------------------
One of you hosts, the other joins. Both press Play on The Witness in Steam (or open witness_launcher.exe),
with Steam running and online. The launcher has four tabs: Play, Saves, Settings, About. On Play you
choose Host a game, Join a friend or Play solo; hosting or joining turns Play into the Lobby, and
Leave brings you back. Settings are saved as you change them.

Over the internet (Steam, nothing to forward):
Host:
1. Play > Host a game > Through Steam > Open lobby. The launcher makes a friends-only Steam lobby
   and shows its number.
2. Invite your friend: "Steam overlay" opens Steam's invite window if the overlay is there;
   or pick your friend in the list next to it and click "Invite friend" (a Steam chat invite);
   or tell them the lobby number.
3. When your friend is in the lobby and Ready, choose what to play (co-op or a race) and press
   Start game. A countdown runs and both games start by themselves.
Joining:
1. Accept the invite in Steam (or "Join Game" on the host in your friends list). If the launcher
   is closed, Steam starts The Witness, which opens the launcher in the host's lobby instead.
   Or Play > Join a friend, paste the lobby number and press Join.
2. Press Ready. The host starts; your game starts with it.
If something is wrong the launcher says so in plain words (Steam not running, offline, lobby full
or gone, a different version of the mod). Everyone needs the same version of the mod.

On a LAN, or by hand (Direct):
Host: Play > Host a game > Direct > Open lobby. "Friends can join by" shows which ways work
(Same network, Tailscale, Internet, Windows Firewall) and what to do about the others.
Joining: Play > Join a friend. Games on your network and your Tailscale devices are listed:
pick one and press Join. Or press Copy invite on the host ("Fox is hosting The Witness.
Join: https://.../#WIT-...") and paste the whole message, the link or the WIT- code into the box,
then Join. The box also takes a Steam lobby number, an address or a computer name. An invite link
(witness-coop://...) opens the launcher with the invite already in the box: check it, then press Join.
Settings > Co-op rules > Who can see my lobby: Open to my networks, or Invite only (not listed;
friends need the invite).
Ports and firewall: Windows asks once to allow witness_launcher.exe and the game through
the firewall: allow them on private networks. Over the internet the launcher asks your router to
open UDP 47100 (the lobby) and 47001 (the host's game) while you host (UPnP / NAT-PMP; Settings >
Network to turn it off). If your router or provider does not allow that, use the Steam lobby
or Tailscale. On a LAN nothing needs forwarding.

A new game or continue: the host chooses in the lobby. "New game" can start with parts of the
island already done. Saves are the game's own, in %APPDATA%\The Witness, as usual.

In the game
-----------
- Hold F1 for the quick guide with every key. Esc pauses as usual.
- T chat (Enter sends, Esc cancels). P pings the point you look at: a pole with your name
  and the distance, an arrow at the edge of the screen when it is off screen.
- Hold G for the emote wheel (Tab turns its pages). Point at a slot and let go.
- Hold X to draw on the world where you look, in your colour; everyone sees it, in puzzles too.
  Shift+X clears your ink, B changes the colour. Z toggles a pen you move over the screen for
  precise lines (Backspace undoes).
- Hold N to talk (push to talk); Y steps through the voice
  channels: nearby players hear you as if you were there, or an open line. Players far away sound like
  a walkie-talkie. (The handheld radio on R is experimental: Settings, Launcher, Experimental.)
- V watches the other player's screen (small picture); F9 changes its size.
- What is shared: solved panels, opened doors, powered panels and lit cables, the lines you draw
  on a panel. Not shared yet: lasers, bridges, plates and a few special puzzles (both of you can
  still do them, each in your own world). The other player is drawn with the game's depth, so
  walls and doors hide them.

Known limits of this alpha
--------------------------
- First play test over the internet. Localhost and LAN are what the automated tests cover.
- Nobody who joins after progress has been made gets the whole world yet; start together.
- Everybody must have the same mod version (the lobby says so if not).
- Steam Deck / Linux (Proton) is untested (see above). Copies of the game that are not Steam build 11370412 are not supported.
- The game's own Launcher.variables (video settings) is written by the launcher (Settings > Display).

Mod managers and scripts
------------------------
coop-mod.json lists what this package puts into the game folder, with SHA-256. Copying those files into the game folder is a complete install.
Files that Vortex deployed (hard links) stay Vortex's: Setup says "Installed by Vortex: update or remove it there" and the launcher offers no
update of its own. Files copied by hand that match this release are taken over by Setup. From a script:
  Witness-Coop-Setup.exe status [--json] [<game folder>]     (on Linux: sh install-linux.sh status ...)
  Witness-Coop-Setup.exe install <game folder> --yes
  Witness-Coop-Setup.exe uninstall <game folder> --yes
Exit codes: 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running.

Uninstall
---------
Open Witness-Coop-Setup.exe and choose Uninstall (on Linux: sh install-linux.sh uninstall). It removes dsound.dll (and puts back a
dsound.dll of another mod it found there), the launcher and the player model, undoes the Proton override, and keeps your co-op settings and logs
(%LOCALAPPDATA%\witness-coop) and your saves. It also removes what the three options added (desktop shortcut, Windows Firewall rule, invite-link handler). Only files this setup installed are touched (each has a
.coop-receipt.json beside it); files it does not own are kept.

What the installer changes
--------------------------
Only the game folder (the single-file Setup also unpacks itself for a moment into %LOCALAPPDATA%\witness-coop\setup\ and deletes that again):
- dsound.dll is added: the co-op mod (the game loads this DLL from its folder; Steam does not know it,
  so a game update leaves it alone). A dsound.dll of another mod is kept as dsound.chain.dll.
- An install of v0.1.0-alpha is undone first: the game's own AnselSDK64.dll goes back (its SHA-256 is
  checked), the old co-op AnselSDK64.dll and AnselSDK64_real.dll are removed.
- witness_launcher.exe (the lobby and settings window) and witness_models\ (the adventurer).
- Your co-op settings (witness_coop.cfg) and the mod's log (witness_proxy.log) are kept in %LOCALAPPDATA%\witness-coop, not in the game folder.
- Only if you leave the options ticked: a desktop shortcut, a Windows Firewall rule "The Witness Co-op (UDP)" for the game and the launcher on
  Private and Public networks (one administrator prompt), and a per-user link handler for witness-coop:// (registry, HKCU) that opens the launcher
  with the invite in its join box.
No Steam files, no saves. No file of the game is changed. (On Linux, install-linux.sh also
writes the one DLL override above into the game's Proton prefix.)

Reporting a problem
-------------------
https://github.com/TommoDotCommo/witness-coop/issues/new
Attach witness_proxy.log from %LOCALAPPDATA%\witness-coop (both players' if you can) and say who hosted,
the mod version (the name of this zip), and what you were doing.

MIT licensed (LICENSE.txt). The Witness itself is not included.


Reporting a problem
-------------------
Press F2 in the game the moment something goes wrong: the game saves the last minute and tells the other players' games to do the
same. Then open the launcher and press About > "Send diagnostics" (it works after a crash too). A zip appears on your desktop. It holds
the logs of your recent runs, a crash dump if the game crashed, your witness_coop.cfg and a summary of your PC. Your Windows and
computer names, IP addresses and Steam IDs are replaced; no saves and no game files are in it, and nothing is uploaded. Attach it to
a bug report at https://github.com/TommoDotCommo/witness-coop/issues/new?template=bug.yml , with your mod version, how you connected
and where on the island you were. The logs are kept in %LOCALAPPDATA%\witness-coop\logs.
