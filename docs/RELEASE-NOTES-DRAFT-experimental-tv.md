# Draft release notes: TV features are experimental

- **TV features are now behind an option, off by default.** The back TV, the sky TV (a giant screen with three audience stools), sitting on
  the stools and the TV camera consent are experimental. They run only when you turn on **Experimental: TV features** (launcher,
  Settings, Launcher; `experimental_tv=1` in `witness_coop.cfg`; it applies the next time the game starts).
- With the option off nothing of them runs, is drawn, or is sent to the other players. Their keys (F10, F11, J, K) are not used by the mod
  and are left out of the quick guide (hold F1); the Controls page keeps their rows (marked "only with Experimental: TV features") so turning the option on brings them back as they were.
- Mixed groups work: a player with the option off simply ignores TV messages. The player with it on sees static on the back TV and
  nobody sits at their sky TV; nothing else changes.
- The partner picture (F9) is a separate feature and is not affected.
