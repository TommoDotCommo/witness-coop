# Draft release notes: the handheld radio is experimental

- **The handheld radio is now behind an option, off by default**, beside the TV features. Turn on **Experimental: handheld radio** (launcher, Settings,
  Launcher; `experimental_radio=1` in `witness_coop.cfg`; it applies the next time the game starts). It covers the R key, the walkie-talkie in your hand and
  on the adventurer, its antenna light, the Radio voice channel and the RADIO mark in the player panel.
- With the option off, R does nothing (it stays bound and is left to the game), Y skips the radio channel, *Who hears you* has no Radio entry, the quick guide
  does not mention the radio, and nothing radio-specific is sent to the other players.
- **A player far away still sounds like a walkie-talkie** with the option off: that is part of ordinary proximity voice. Its two launcher rows
  (*Radio sound when far away*, *Radio range*) are now hidden unless you changed them; the cfg keys (`voice_radio`, `voice_radio_range`) work as before.
- Mixed groups work: someone with the option off still hears a radio-channel speaker (band-limited, any distance) and sees no prop, arm pose or RADIO mark on them.
- The old *Handheld radio* row is replaced by the experimental option; `radio_enabled=0` in the cfg still disables it.
