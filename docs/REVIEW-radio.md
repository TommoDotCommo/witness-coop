# Review: the radio (for the owner's decision, 2026-11-03)

Nothing is removed by this review. It says what the radio is, what it costs and what each choice would take.
Facts below were read from the code at the start of this task (base `8f1411d`) and from the committed tests; nothing here was measured with players.

## 1. What the radio is to a player

"Radio" is four things that share a name:

| What | Where the player meets it | Setting (`witness_coop.cfg`) | Default |
|---|---|---|---|
| **Handheld radio** | A walkie-talkie prop in your hand (first person, bottom right of the screen) and in the others' view of your adventurer (right arm raised, antenna light). Hold **R** to talk on it. | `radio_enabled`, key `key_radio` (R); launcher: Settings, Voice and sound, *Handheld radio*; Controls, *Radio (hold)* | on |
| **Radio channel** | One of four voice channels (proximity, **radio**, open line, one on one). Everybody hears you, at any distance, band-limited with hiss and squelch, from the middle of the head. Chosen with **Y** (next channel) or *Who hears you* in the launcher. Holding R switches to it for as long as you hold. | `voice_channel=radio` | proximity |
| **Radio sound when far away** | A player talking on the proximity channel who is further away than the hearing range turns into a walkie-talkie voice instead of fading out. | `voice_radio` (launcher: *Radio sound when far away*) | on |
| **Radio range** | The distance beyond which even that far voice is silent. | `voice_radio_range` (launcher: *Radio range*, 0 = unlimited) | 0 (unlimited) |

Related, not radio proper: *Hearing range* (`voice_range`, default 25 m) is where the proximity voice hands over to the radio sound.
The roster/player panel (hold Tab) shows a RADIO mark beside a player who is transmitting; voice volume and mute there apply to every channel.

## 2. How it differs from proximity voice and the other channels

- **Proximity (default).** Spatial, louder when close, from the side the speaker is on, dulling with distance. Beyond 25 m it crossfades to the same radio sound, so by default (`voice_radio=1`, `voice_radio_range=0`) **everybody already hears everybody at any distance**; the far voice just sounds like a walkie-talkie.
- **Radio channel / hold R.** Same radio sound, but also close up (no spatial placement, no distance fade). With the defaults the only audible difference from proximity is for players who are within 25 m of each other (they hear a filtered voice from the middle instead of a spatial one), plus the prop and the arm pose.
  It matters when the group has turned the far-away radio off (`voice_radio=0`) or set a radio range: the channel then still reaches everybody.
- **Open line (global).** Everybody, any distance, undistorted, from the middle. It does the same job as the radio channel without the effect, and needs no prop.
- **One on one (direct).** Only the named player hears you, with the radio sound. It is the one channel the radio does not duplicate.
- **Push to talk (B / mouse side button).** Unrelated to which channel: it sends on whichever channel is selected. R is "push to talk on the radio channel".

## 3. What it adds to the code

Counts are lines that mention radio (`grep -ci radio`), so they overstate the pure-radio part in files where voice is mixed in.

| Area | Files | Lines | What |
|---|---|---|---|
| Radio module | `proxy/radio/radio.h` (115 lines), `radio_game.cpp` (32), `radio_game.h` | whole files | hold state, raise animation, prop mesh (732 vertices, procedural), first-person placement, guide/roster layouts |
| Voice mixer | `proxy/voice/voice.cpp` 70-94, 109-120, 180-199; `voice.h` | ~32 | radio DSP (350-3000 Hz filter, saturation, hiss, squelch), channel gains, the fade from proximity to radio, radio range |
| Capture/transmit | `proxy/voice_audio.cpp` 65-66, 92, 160-168, 196-201 | 15 | hold key, forcing the radio channel while R is held, HUD channel name, `VoiceRadioTx` |
| Key | `common/bindings.def:14`, `proxy/social.cpp:214,258` | 3 | `key_radio` (R); shown in guide and Controls from the one table |
| Presence on the wire | `proxy/core/sync_core.cpp` 726-769 | 11 | flag bits 2 (transmitting) and 3 (equipped), byte 19 voice channel; validity rules (`radioTx` needs equipped, speaking, channel radio) |
| Drawing | `proxy/overlay.cpp` 250-270 (first-person prop pass), `proxy/model_gpu.cpp` 463, 498, 648 (hand attachment, arm overlay), `proxy/model/model.cpp,.h` (pose layer) | ~36 | a separate view-model draw pass plus the right-arm overlay on the remote figure |
| Settings | `proxy/config.h/.cpp` (`radioEnabled`, `voiceRadio`, `voiceRadioRange`, two render mutants), `launcher/shell.cpp:53-60` | 10 + 4 rows | four launcher rows (*Who hears you* also lists Radio) |
| Guide / docs | `common/guide.h:197`, `docs/radio.md`, `features.md`, `README.md`, `dist/README.txt` | ~12 | player-facing text |
| Tests | `proxy/radio/radio_test.cpp` (117 lines, **19 mutants** in `proxy/radio/Makefile`), `proxy/voice/voice_test.cpp` (12 voice mutants, 5 about radio), `tools/qa_radio.py` + `tools/win/wradio.py` (live, clean + 5 mutants + disabled run), radio parts of `tools/qa_voice.py` / `wvoice.py` | ~300 | game-free: `make -C proxy/radio test`, `make -C proxy/voice test`; live: `tools/qa.sh radio` |

Not counted: the settings are also part of the voice-and-sound section the standard (E) expects, so *Radio range* and *Radio sound when far away* are the only rows with no counterpart in the standard's shared row list.

## 4. Known issues and risks

- **Redundancy.** With defaults, far players are already heard through the radio sound, and the open line covers "everybody, clearly". The handheld radio's audible gain over proximity is small for a group that stays together; its main effect is the prop and animation.
- **Never player-tested for look and latency.** `docs/radio.md` says the prop's rendering and visual quality "still require the live review"; added latency of the radio path is "unmeasured".
- **Three names for one idea.** Players must learn "Radio" (channel), "Handheld radio" (prop/key), "Radio sound when far away" and "Radio range"; the last two are settings of the *proximity* voice, not of the radio. This is likely to confuse (the launcher hint "beyond it the voice turns into radio" helps only a little).
- **Key R** is a free key in this mod's table, but the standard's list of checked-free keys (Tab, V, B, H, Y) does not cover R; the game's own R use was not re-verified here (unknown).
- **Extra draw pass.** The first-person prop is a second view-model pass drawn after the world, with its own placement maths for every aspect ratio (tested at 40-110 degree FOV in `radio_test.cpp`), plus an arm overlay that needs the finished adventurer's bone names. Each is a place for a rendering bug on an untested aspect/model pack.
- **Wire compatibility.** Presence bits 2/3 and byte 19 are part of the 36-byte record; older peers ignore them (documented), so removal on the wire is safe for them; keeping them costs nothing on the wire.
- **Experimental TV option does not cover it.** `experimental_tv` (this task) does not touch the radio.

## 5. How often a group would use it

No usage data exists (no telemetry; the lab has had no player sessions of the radio). From the design: a pair or small group who play near each other talk on the default channel and never press R. Someone who wanders far away is already heard through the far-voice radio sound. R is used by someone who wants to be heard clearly across the island *and* has turned the automatic far voice off or limited it, or who likes the prop. Expected frequency: rare, for a small minority of sessions. (Unknown until beta feedback.)

## 6. Options and recommendation

| Option | What it takes | Cost / risk |
|---|---|---|
| **A. Keep as is** | nothing | keeps ~15 files of surface, a render pass and the naming confusion for a feature few will use |
| **B. Keep, simplify** | Fold the settings: drop *Radio sound when far away* and *Radio range* from the launcher (keep the behaviour at its defaults, config keys still read), so the launcher shows only *Handheld radio* and *Who hears you*. Rename *Handheld radio* to "Walkie-talkie prop and R key". | ~10 lines of launcher rows and tests; no wire or render change; smallest confusion cut |
| **C. Put behind the experimental option too** | Gate `key_radio`, the prop (overlay.cpp 261, model_gpu radio pass), the hold, and the *Radio* entry of the channel list (and the Radio mark) on a flag the same way `experimental_tv` gates the TV (an `experimentalRadio` cfg key or reuse a single "Experimental" group). Far-voice radio sound stays (it is part of ordinary proximity voice). | about a day: one gate function, guide/Controls hiding, one T0 test with mutant, `tools/qa.sh radio` gets the cfg key; the radio tests keep their value |
| **D. Remove** | Delete `proxy/radio/` and its test, `key_radio` from `bindings.def` (plus the migration/guide tests that list it), the radio branches in `voice_audio.cpp`, `social.cpp`, `overlay.cpp`, `model_gpu.cpp`, `model.cpp`, the presence bits 2/3 (leave byte 19 and the channel enum: `kRadio` is also the base of `kDirect`), the four launcher rows, guide/README/features/radio.md text, `tools/qa_radio.py`, `wradio.py`, the 19+5 mutants, `qa.sh` entry. Keep the far-voice radio *filter* (`ProcessRadio`) because proximity and one on one use it. | ~2 days with the tests; touches the voice mixer and the wire record, so every voice and sync scenario should rerun once before a release |

**Recommendation: B now, with C as the fallback if the owner wants the beta surface smaller.**
Reasons: the far-voice radio sound is a real benefit that players get without doing anything and stays in all options; the only part that is optional is the hand prop, its key and the extra channel entry. Removing (D) would cut working, tested code and still leave the radio filter in place for one on one; gating (C) hides the prop and R for the beta without losing it. B costs the least and fixes the one concrete problem found (four "radio" names), but if the owner's goal is "fewer things to explain in the beta", C is the cleaner choice and D can follow after beta feedback shows nobody pressed R.

The owner decides.
