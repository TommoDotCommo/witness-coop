witness-coop @VERSION@ - co-op for The Witness
==============================================

Walk the island together: both of you see each other as the adventurer, every panel
one of you solves is solved for the other, doors open for both, and you can chat, point
things out, draw on the world and talk. Alpha for a first play test: expect rough edges.
Project page and bug reports: https://github.com/TommoDotCommo/witness-coop

You need The Witness on Steam, build 11370412 (the current one). Setup checks the game's
exe and refuses any other build: the mod uses that build's addresses. Every player needs
their own copy and this same mod version (the launcher shows it, and offers to update itself when a newer release is out: Play page, "About and updates"). If Setup says your build is not supported, see
"Setup says my build is not supported" below.


Install (easiest): the single file
----------------------------------
1. Download Witness-Coop-Setup.exe from the releases page and double-click it. It is one file and
   needs nothing else: do not open it from inside a zip. (If you see "Setup was started from
   inside a zip file", this is the cause; use the single file, or Extract All first.)
2. Setup finds The Witness in your Steam libraries (or asks for the folder), checks the build and
   shows what it will change. Click Install. Windows asks for administrator permission only if
   the game folder is protected.
3. Click Play (launcher) when it finishes, or create a desktop shortcut. From now on start the
   game through witness_launcher.exe in the game folder, not from Steam, when you want co-op.
   Starting it from Steam still works: alone, with the mod loaded but nobody to connect to.

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
3. Start witness_launcher.exe from the game folder.
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
- The launcher is a Windows program and has to run in the game's own Proton prefix. In Steam
  (Desktop mode): Games > Add a Non-Steam Game > Browse to witness_launcher.exe in the game folder.
  Open its Properties: Start In = the game folder; Compatibility = force the same Proton version as
  The Witness; Launch Options =
    STEAM_COMPAT_DATA_PATH="<your Steam library>/steamapps/compatdata/210970" %command%
  Start that entry instead of the game for co-op. Its Lobby starts the game inside the same prefix.
  Starting The Witness from Steam as usual loads the mod alone, like on Windows.
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
One of you hosts, the other joins. Both open witness_launcher.exe, with Steam running and online.

Over the internet (Steam, nothing to forward):
Host:
1. Lobby tab > Host on Steam. The launcher makes a friends-only Steam lobby and shows its number.
2. Invite your friend: "Invite (overlay)" opens Steam's invite window if the overlay is there;
   or pick your friend in the list next to it and click "Invite friend" (a Steam chat invite);
   or tell them the lobby number.
3. When your friend is in the lobby, choose Co-op (or a race) and press Start the game.
   A countdown runs and both games start by themselves.
Joining:
1. Accept the invite in Steam (or "Join Game" on the host in your friends list). If the launcher
   is closed, Steam starts The Witness, which opens the launcher in the host's lobby instead.
   Or paste the lobby number into the Invite field and press Join.
2. Tick Ready. The host starts; your game starts with it.
If something is wrong the launcher says so in plain words (Steam not running, offline, lobby full
or gone, a different version of the mod). Everyone needs the same version of the mod.

On a LAN, or by hand (Direct):
Host: Lobby tab > Host direct (LAN). Send your friend the invite shown (address:port/CODE) with the
Copy button. Joining: paste the invite > Join.
Ports and firewall: Windows asks once to allow witness_launcher.exe and the game through
the firewall: allow them on private networks. Direct play over the internet needs the host to
forward UDP 47100 (the lobby) and UDP 47001 (the host's game), and sometimes the joiner 47002;
use the Steam lobby instead. On a LAN nothing needs forwarding.

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
- Hold N to talk (push to talk); hold R to talk on the radio (everyone, at any distance). H steps
  through the voice channels: nearby players hear you as if you were there, radio, or an open line.
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
- The game's own Launcher.variables (video settings) is written by the launcher's Video tab.

Uninstall
---------
Open Witness-Coop-Setup.exe and choose Uninstall (on Linux: sh install-linux.sh uninstall). It removes dsound.dll (and puts back a
dsound.dll of another mod it found there), the launcher and the player model, undoes the Proton override, and keeps witness_coop.cfg (your co-op settings),
witness_proxy.log and your saves. Only files this setup installed are touched (each has a
.coop-receipt.json beside it); files it does not own are kept.

What the installer changes
--------------------------
Only the game folder (the single-file Setup also unpacks itself for a moment into %LOCALAPPDATA%\witness-coop\setup\ and deletes that again):
- dsound.dll is added: the co-op mod (the game loads this DLL from its folder; Steam does not know it,
  so a game update leaves it alone). A dsound.dll of another mod is kept as dsound.chain.dll.
- An install of v0.1.0-alpha is undone first: the game's own AnselSDK64.dll goes back (its SHA-256 is
  checked), the old co-op AnselSDK64.dll and AnselSDK64_real.dll are removed.
- witness_launcher.exe (the lobby and settings window) and witness_models\ (the adventurer).
- witness_coop.cfg is written by the launcher when you play; witness_proxy.log is the mod's log.
No registry, no Steam files, no saves. No file of the game is changed. (On Linux, install-linux.sh also
writes the one DLL override above into the game's Proton prefix.)

Reporting a problem
-------------------
https://github.com/TommoDotCommo/witness-coop/issues/new
Attach witness_proxy.log from the game folder (both players' if you can) and say who hosted,
the mod version (the name of this zip), and what you were doing.

MIT licensed (LICENSE.txt). The Witness itself is not included.


Reporting a problem
-------------------
Press F2 in the game the moment something goes wrong: the game saves the last minute and tells the other players' games to do the
same. Then open witness_launcher.exe and press "Send diagnostics" (it works after a crash too). A zip appears on your desktop. It holds
the logs of your recent runs, a crash dump if the game crashed, your witness_coop.cfg and a summary of your PC. Your Windows and
computer names, IP addresses and Steam IDs are replaced; no saves and no game files are in it, and nothing is uploaded. Attach it to
a bug report at https://github.com/TommoDotCommo/witness-coop/issues/new?template=bug.yml , with your mod version, how you connected
and where on the island you were. The logs are kept in %LOCALAPPDATA%\witness-coop\logs.
