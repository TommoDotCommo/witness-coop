The Witness Co-op @VERSION@
==========================

A co-op mod for The Witness. You and your friends walk the same island and see each
other. When one of you solves a panel it's solved for everyone, and the doors and
cables it powers open and light up in every game. You can talk, chat, point at
things, draw on the world, or race each other.

This is a beta: expect a few rough edges, and please report what you find (see
"Reporting a bug" at the end).

Project page: https://github.com/TommoDotCommo/witness-coop


What you need
-------------
- The Witness on Steam, the current version (build 11370412). Every player needs
  their own copy. GOG and Epic copies aren't supported yet.
- Everyone on the same version of this mod. The launcher tells you if you aren't and
  offers to update.


Install
-------
Easiest: run Witness-Coop-Setup.exe (the single-file download from the release page).
If you're using this zip instead, right-click it > Extract All first, then run
Witness-Coop-Setup.exe from the extracted folder. Setup won't run from inside a zip.

1. Setup finds The Witness in your Steam libraries (or use Browse) and checks the
   game version. Click Install.
2. Press Play in Setup when it's done, or press Play on The Witness in Steam from now
   on. The co-op launcher opens first and starts the game for you.

Setup adds dsound.dll (the mod), witness_launcher.exe and the witness_models folder
to the game folder. None of the game's own files are changed, and your saves aren't
touched. The desktop shortcut, firewall rule and invite-link options are optional.

If Windows says "Windows protected your PC": the files aren't code-signed yet. Click
More info, then Run anyway.

By hand, without Setup:
1. Close the game. In Steam, right-click The Witness > Manage > Browse local files.
2. Copy dsound.dll, witness_launcher.exe and the witness_models folder from this zip
   into that folder. If there's already a dsound.dll from another mod, rename that
   one to dsound.chain.dll first.
3. Press Play on The Witness in Steam.

Coming from v0.1.0-alpha: just run the new Setup, it moves you over. By hand: delete
AnselSDK64.dll, rename AnselSDK64_real.dll to AnselSDK64.dll (or use Steam's "Verify
integrity of game files"), then copy in the new files as above.


Steam Deck and Linux
--------------------
Open a terminal in the extracted zip and run:

    sh install-linux.sh install

It finds the game, installs the files and tells Proton to load the mod's dsound.dll.
The game needs to have run once under Proton first. If the script can't set that up,
add this launch option to The Witness in Steam:

    WINEDLLOVERRIDES="dsound=n,b" %command%

"sh install-linux.sh uninstall" removes it again, and "status" shows what's installed.


Playing together
----------------
One person hosts, everyone else joins. Everyone opens the launcher and goes to Play.

Through Steam (easiest over the internet):
  Host: Host a game > Through Steam > Open lobby. Invite friends with the Steam
  overlay, with Invite friend in the launcher, or by giving them the lobby number.
  Friends: accept the invite in Steam. If the launcher isn't open, Steam opens it.
  Nobody needs to open any ports.

Same network, Tailscale, or direct:
  Host: Host a game > Direct > Open lobby. "Friends can join by" shows which ways
  work and what to fix. Copy invite copies a message to send to friends.
  Friends: Join a friend. Games on your network and your Tailscale devices are
  listed. Otherwise paste what the host sent (the message, the link, the WIT- code,
  an IP address or a computer name) into the box and press Join.
  Over the internet the launcher asks your router to open UDP 47100 and 47001 while
  you host. If that doesn't work, use Steam or Tailscale.

In the lobby you can chat and talk. The host picks co-op or a race, and continue or
a new game (optionally with parts of the island already done). Everyone ticks Ready,
the host presses Start game, and the games start together. Friends can join a game
that's already going: their world catches up when it loads.


Keys
----
Hold F1 in the game or the launcher to see all of them, including ones you changed.

  T                     chat (Enter sends, Esc cancels)
  V or middle mouse     ping the spot you're looking at
  B or mouse side btn   push to talk
  Y                     next voice channel (nearby, open line, one on one)
  hold H                emote wheel (Tab changes page)
  hold X                draw on the world (Shift+X clears, mouse wheel = colour)
  Z                     drawing canvas, a pen you move on the screen (Backspace undoes)
  hold Tab              player panel: mute, volume, watch a player, leave
  [ and ]               watch the previous / next player
  F9                    size of the other player's picture: small, large, off
  F3                    faster mouse while tracing a puzzle
  F2                    mark a problem for a bug report

Controller: the co-op actions are on the View button. Tap to ping, hold for the
player panel. While holding View: D-pad up emotes, D-pad down chat, D-pad left/right
watch players, RB quick chat, Y the quick guide.

Change keys in the launcher under Settings > Controls.


What's shared
-------------
Solved panels, puzzle lines as they're drawn, doors opening and closing, panels
powering on, cables and lasers. Bridges and moving platforms aren't shared yet.
In a race nothing is shared, on purpose.


Updating
--------
The launcher checks for a new release once a day and shows a banner on Play when
there is one. Update replaces only the mod's own files. If you installed with Vortex,
update through Vortex.


Uninstall
---------
Run Witness-Coop-Setup.exe and choose Uninstall (Linux: sh install-linux.sh
uninstall). It removes only what it installed. Your settings and logs in
%LOCALAPPDATA%\witness-coop and your saves stay.


Troubleshooting
---------------
Setup says the game isn't supported, or the game says "Co-op is off":
  The mod only works with Steam build 11370412. In Steam, right-click The Witness >
  Properties > Betas and choose no beta, then Installed Files > Verify integrity of
  game files, and run Setup again. If Steam is already up to date, the game was
  updated after this mod: the game runs normally without co-op until a mod update.

My friend's game isn't in the list:
  The list only shows your own network and Tailscale. Use a Steam lobby, or paste
  the host's invite. If Windows asks about the firewall, allow the launcher and the
  game on private networks.

No voice:
  Check Settings > Voice and sound, mute/deafen in the lobby, and the player panel
  (hold Tab). On a one-on-one channel only that player hears you.


Reporting a bug
---------------
1. Press F2 the moment something goes wrong. It saves the last minute and marks the
   same moment in every player's log.
2. Open the launcher > About > Send diagnostics. A zip appears on your desktop. Your
   computer name, user name, IP addresses and Steam IDs are replaced in it, and it
   holds no saves or game files. Nothing is uploaded.
3. Attach the zip to a bug report:
   https://github.com/TommoDotCommo/witness-coop/issues/new?template=bug.yml
   One from each player is best, the host's first.


Licence
-------
MIT (LICENSE.txt). This is a fan project, not affiliated with Thekla, Inc., and it
doesn't include any of the game's files.
