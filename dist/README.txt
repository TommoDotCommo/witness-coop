witness-coop @VERSION@ - co-op for The Witness
==============================================

Walk the island together: both of you see each other as the adventurer, every panel
one of you solves is solved for the other, doors open for both, and you can chat, point
things out, draw on the world and talk. Alpha for a first play test: expect rough edges.
Project page and bug reports: https://github.com/TommoDotCommo/witness-coop

You need The Witness on Steam, build 11370412 (the current one). Setup checks the game's
exe and refuses any other build: the mod uses that build's addresses. Every player needs
their own copy and this same mod version.


Install
-------
1. Unzip this folder anywhere (not inside the game folder).
2. Double-click Witness-Coop-Setup.exe. It finds The Witness in your Steam libraries (or asks for
   the folder), checks the build and shows what it will change. Click Install. Windows asks for
   administrator permission only if the game folder is protected.
3. Click Play (launcher) when it finishes, or create a desktop shortcut. From now on start the
   game through witness_launcher.exe in the game folder, not from Steam, when you want co-op.
   Starting it from Steam still works: alone, with the mod loaded but nobody to connect to.

If Windows shows "Windows protected your PC", click "More info" > "Run anyway".
(Setup is a small program that runs PowerShell scripts you can read in Notepad:
installer-gui.ps1 and witness-coop.ps1.)

Playing with a friend
---------------------
One of you hosts, the other joins. Both open witness_launcher.exe.

Host:
1. Lobby tab > Host a lobby. The lobby port is 47100 (UDP). Leave "Friends reach this PC at"
   blank unless you know your public address.
2. Send your friend the invite shown (address:port/CODE), with the Copy button.
3. When your friend is in the lobby, choose Co-op (or a race), tick Ready, press Start.
   A countdown runs and both games start by themselves.

Joining:
1. Lobby tab > paste the invite > Join. You see the host, can chat and talk.
2. Tick Ready. The host starts; your game starts with it.

Ports and firewall: Windows asks once to allow witness_launcher.exe and the game through
the firewall: allow them on private networks. Over the internet the host forwards UDP 47100
(the lobby) and UDP 47001 (the host's game) on their router to their PC. The joining player's
game uses UDP 47002; most home routers let the reply through by themselves, so try without
forwarding first and forward 47002 if the two games never see each other. A VPN between the two
PCs (Tailscale, ZeroTier, Hamachi) is the simplest alternative: use that address in the invite.
On a LAN nothing needs forwarding.

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
- The game's own Launcher.variables (video settings) is written by the launcher's Video tab.

Uninstall
---------
Open Witness-Coop-Setup.exe and choose Uninstall. It puts the game's own AnselSDK64.dll back,
removes the launcher and the player model, and keeps witness_coop.cfg (your co-op settings),
witness_proxy.log and your saves. Only files this setup installed are touched (each has a
.coop-receipt.json beside it); files it does not own are kept.

What the installer changes
--------------------------
Only the game folder:
- AnselSDK64.dll becomes the co-op mod (the game loads this DLL from its folder); the game's own
  AnselSDK64.dll is kept as AnselSDK64_real.dll, and as AnselSDK64.dll.coop-original for uninstall.
- witness_launcher.exe (the lobby and settings window) and witness_models\ (the adventurer).
- witness_coop.cfg is written by the launcher when you play; witness_proxy.log is the mod's log.
No registry, no Steam files, no saves. The game executable is never changed.

Reporting a problem
-------------------
https://github.com/TommoDotCommo/witness-coop/issues/new
Attach witness_proxy.log from the game folder (both players' if you can) and say who hosted,
the mod version (the name of this zip), and what you were doing.

MIT licensed (LICENSE.txt). The Witness itself is not included.
