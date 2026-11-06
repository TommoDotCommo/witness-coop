# The Witness Co-op v0.2.0-beta.1

The first beta. Download **`Witness-Coop-Setup.exe`**, run it, and press Play. If you played the alpha, run the new
Setup and it will move you over. Everyone in a game needs this same version.

The [README](../README.md) covers installing, hosting and joining.

## Highlights

- **Your game's files are no longer changed.** The mod is now a `dsound.dll` that sits next to the game instead of
  replacing one of its files. Steam updates and "Verify integrity of game files" leave it alone, and installing by
  hand is just copying three things into the game folder.
- **Play over the internet through Steam.** Host a friends-only Steam lobby and invite friends through Steam. Steam
  relays the connection, so nobody has to open ports.
- **Finding each other is much easier.** Join a friend lists games on your network and your Tailscale devices on
  its own, and one box takes any kind of invite: the message, a link, a `WIT-` code, a Steam lobby number, an address.
- **Press Play in Steam** and the co-op launcher opens, including in Steam Deck Game Mode.
- **A redesigned launcher** with four tabs (Play, Saves, Settings, About), drawn in the look of the game.
- **Controller support** in the launcher and in the game.
- **A new adventurer** with a more expressive face, 24 hand-animated emotes, and a camera that swings round so you see
  yourself emote.

## Installing

- **One file.** `Witness-Coop-Setup.exe` carries everything. It finds every copy of the game it can and shows each
  one with its version check. Options for a desktop shortcut, a Windows Firewall rule and opening invite links in the
  launcher; Uninstall removes all three.
- **Clear messages when your game isn't the supported version**, with what to do about it. If a game update arrives
  after you installed, the mod switches itself off with one message and the game runs normally.
- **From the alpha:** Setup and the launcher's updater put the game's own `AnselSDK64.dll` back (after checking it's
  the original) and install the new layout.
- **Another mod's `dsound.dll`** (an ASI loader, say) is kept as `dsound.chain.dll` and still loaded. Uninstall puts it
  back.
- **Mod managers.** A Vortex extension and a Mod Organizer 2 plug-in for The Witness are included with the release.
  Setup recognises a Vortex install and leaves it to Vortex.
- **Nothing is written to the game folder while you play.** Settings and logs live in `%LOCALAPPDATA%\witness-coop`.
- **Linux and Steam Deck:** `install-linux.sh` installs the mod and sets up the Proton DLL override for you.
- **Setup can be scripted:** `Witness-Coop-Setup.exe status|install|uninstall`.

## Playing together

- **Steam lobbies** for up to 16 players. Invite through the Steam overlay, a Steam chat invite from the launcher, or
  the lobby number. Accepting an invite with the launcher closed opens it straight into the lobby.
- **Join a friend** lists games on every network your PC is on, on Tailscale, and hosts you've joined before,
  refreshing every few seconds, with each game's player count and ping.
- **Copy invite** copies a message with a link your friends can paste anywhere in the launcher. It carries every way
  of reaching you that works.
- **Friends can join by** (in the lobby) checks each way in: Steam, same network, Tailscale, internet and Windows
  Firewall, and says what to fix. While you host a direct lobby, the launcher asks your router to open the ports.
- **Invite-only lobbies:** Settings > Co-op rules > Who can see my lobby.
- **Versions:** the launcher shows everyone's version, refuses a mismatch with both versions named, and offers to update.

## The launcher

- Four tabs instead of nine. Play has your name and three choices: host, join or play solo.
- Settings save as soon as you change them, in sections with **Reset section**. Volumes and ranges are sliders, and
  you set a key by pressing it.
- It looks like the game: a painted sea for a banner, faint puzzle panels behind the pages, the game's yellow, and a
  line that traces round the button under your pointer the way a puzzle line does. The Saves tab shows each save's picture.
- Works with a controller (stick or D-pad, A, B, LB/RB for tabs) and fills the screen on a Steam Deck.
- The Setup window and the launcher open faster and no longer flicker.
- Updates appear as a banner on Play. Beta players are offered beta updates; others aren't.

## In the game

- **The co-op keys have moved** to match the other co-op mods. If you never changed them:

  | Action | Was | Now |
  |---|---|---|
  | Ping | `P` | `V` or middle mouse |
  | Push to talk | `N` | `B` or mouse side button |
  | Emote wheel (hold) | `G` | `H` |
  | Next voice channel | `H` | `Y` |
  | Ink colour while drawing | `B` | mouse wheel |
  | Player list | `F5` | hold `Tab` |
  | Watch a player | `V` | player panel (hold `Tab`), or `[` and `]` |

  Keys you changed yourself stay as you set them.
- **Hold Tab for the player panel:** everyone's ping, distance and voice, with Watch, Mute and volume per player,
  name tags, Copy invite and Leave.
- **Controller co-op** on the View button: tap to ping, hold for the player panel, and with View held, emotes, chat
  (Steam's keyboard or one on screen), watching players, quick chat and the guide. Voice is automatically push-to-talk
  on the keyboard and open mic on a pad.
- **A Steam Input layout** for the Steam Deck and other pads (in the repository's `dist/steam-input` folder), with L4
  as the View button and R4 as push to talk.
- **Join and leave notices:** "Fox joined", "Lost Fox, waiting up to 30 s", "Reconnecting to Fox".
- **The co-op text uses the game's own typeface**, read from your copy of the game.
- **The symmetry panels** now show the other player's mirrored line as well as the one they're drawing.

## The adventurer

- A rebuilt, more expressive face, a tailored coat and boots, and real finger bones (so the thumbs up is a thumbs up).
- **24 hand-animated emotes**, with proper weight shifts and follow-through.
- **See yourself emote:** the camera swings behind you while you emote and comes back when you move, click or open a
  menu. It stays first person when you're against a wall. Turn it off under Settings > Players and interface.

## Fixes

- Other players are hidden behind walls at every anti-aliasing setting, not only the default one.
- Name tags and markers are hidden behind walls too, and fade when a player is right in front of your camera.
- Footsteps are quieter and carry less far.
- Holding down a toggle key no longer flips it on and off repeatedly.
- A name tag's voice bars no longer cover the ping.

## Experimental, off by default

The handheld radio (hold `R`) and the TV features (a back-mounted TV showing your view, and a big screen with stools)
are now switched off unless you turn them on in Settings > Launcher. Players who leave them off aren't affected by
players who turn them on. Far-away players still sound like a walkie-talkie either way.

## Known issues

- Only Steam build 11370412 of The Witness is supported.
- Bridges and moving platforms aren't shared between players yet.
- The release isn't code-signed yet, so Windows may show "Windows protected your PC". Click More info, then Run anyway.
- A mod that replaces DirectSound completely won't work alongside co-op.
