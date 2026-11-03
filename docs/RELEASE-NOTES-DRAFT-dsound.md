# Release notes draft: the drop-in install (proxy as dsound.dll)

Lines for the next beta's notes. Not published.

- **The game's own files are no longer changed.** The co-op mod is now `dsound.dll` in the game folder (the game imports DirectSound but does
  not ship it, so Windows loads the mod's copy; the mod passes every sound call to Windows' own `dsound.dll`). The game's `AnselSDK64.dll`
  stays the game's file. A Steam update or *Verify integrity of game files* no longer turns co-op off silently, and installing by hand is
  copying three things into the game folder (`dsound.dll`, `witness_launcher.exe`, `witness_models\`), which mod managers can do too.
- **Coming from v0.1.0-alpha:** Setup, `install-linux.sh` and the launcher's updater put the game's own `AnselSDK64.dll` back (only after
  checking its SHA-256), remove the old proxy and `AnselSDK64_real.dll`, and install the new layout. A file that is not the game's own stops
  this with a plain message and nothing is changed; *Verify integrity of game files* fixes it. An interrupted migration finishes on the next run.
- **Another mod named `dsound.dll`** (an ASI loader, for example) is kept as `dsound.chain.dll` and loaded by the co-op mod right after itself;
  uninstall puts it back. Its own DirectSound exports are not used (a full DirectSound replacement does not combine with co-op).
- **Steam Deck / Linux:** `install-linux.sh` writes the DLL override for `witness64_d3d11.exe` into the game's Proton prefix (with a backup of
  `user.reg`; uninstall undoes exactly that). The prefix appears after the game has run once under Proton; without it the script prints the
  launch option `WINEDLLOVERRIDES="dsound=n,b" %command%`. Still untested on a real Deck.
- Crash reports and `tools/diag_report.py` name the proxy `dsound.dll` (reports from the alpha's `AnselSDK64.dll` are still understood); the
  diagnostics list `dsound.dll`, `dsound.chain.dll` and any leftover of the old layout.
