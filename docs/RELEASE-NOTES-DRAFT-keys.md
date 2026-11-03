# Draft release notes: shared co-op keys and the player panel

## Moved keys

The co-op keys now match the other co-op mods. If you never changed a key, these are the new ones;
a key you changed yourself stays as you set it. The game writes this list once (`moved_keys.txt`,
in your per-user folder) for the launcher's What's new.

| Action | Was | Is now |
|---|---|---|
| Ping a place | `P` | `V` or the middle mouse button |
| Push to talk | `N` | `B` or the mouse side button (XBUTTON1) |
| Emote wheel (hold) | `G` | `H` |
| Next voice channel | `H` | `Y` |
| Ink colour while drawing | `B` | the mouse wheel |
| Player list | `F5` | hold `Tab` |
| Watch a player | `V` | the player panel (hold `Tab`, click Watch); `[` and `]` still step through the players |

If one of your own keys sits on a shared key (for example Chat on `V`), you keep it: Ping stays on
the middle mouse button, and the change is written to the log. Mouse buttons and the wheel can be
bound in the Controls page (`MMB`, `XBUTTON1`, `XBUTTON2`, `WHEEL`).

## Hold-Tab player panel

Hold `Tab`: one row per player (colour, name, host mark, ping, speaking light, distance or state), with
Watch, Mute and a voice volume per player, then voice volume for everybody, name tags, partner picture
size, Copy invite (when the launcher shared an invite) and Leave (it asks first and says what happens to
the others). Arrows, Enter, M, Backspace and Esc work without a mouse. Release `Tab` to close it.
Kick is not offered: the lobby cannot remove a player yet.

## Notices

"Fox joined", "Fox left", "Lost Fox, waiting up to 30 s", "Reconnecting to Fox", "Fox has a different version (v0.4.0)".
