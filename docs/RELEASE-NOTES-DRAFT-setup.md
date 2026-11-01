# Draft release-note lines: setup that a stranger can run (not tagged, not published)

- **One-file Setup.** `Witness-Coop-Setup.exe` is now the main download: one file that carries the mod, the launcher, the player model and the scripts. It checks its
  contents against a SHA-256 compiled into it, unpacks into a fresh per-version folder under `%LOCALAPPDATA%\witness-coop\setup\` (never reusing an existing folder, refusing links),
  and removes it when the window closes. The zip stays for manual installs.
- **"Extract first" is now a plain message.** The Setup inside the zip says "Setup was started from inside a zip file" and points to the one-file download instead of a vague missing-files error.
- **Plain words for an unsupported game.** If your copy of The Witness is not Steam build 11370412, Setup says what it found (store, file version, SHA-256 start), what it needs, and what to do
  (Steam: Properties > Betas > None, then Verify integrity), with a README FAQ link. Other stores are not supported yet. Nothing is changed.
- **The mod no longer touches a game it does not know.** After a game update the mod notices the unknown exe in memory at start, patches nothing, forwards to the game's own Ansel DLL and shows one
  message ("Co-op is off"). The launcher refuses to start such a game and says why.
- **Manual install and Steam Deck / Linux.** The README and README.txt list which files go where, the `AnselSDK64_real.dll` rename, the launcher, the model folder and uninstall by hand.
  `install-linux.sh` does it with backups and receipts shared with the Windows Setup. **Proton is untested**: a native `AnselSDK64.dll` is expected to load without an override and the launcher
  runs as a non-Steam game in the game's prefix.
- **Ready for code signing.** `tools/release.sh` signs every exe and dll with `SIGN_CMD` before hashing and embedding, verifies afterwards, and warns when unset. `docs/SIGNING.md` has the steps,
  including Microsoft's false-positive submission. This release is unsigned unless a certificate was used.

Checks (game-free): `tools/qa_installer.sh`, `make -C common test`, `make -C launcher test`, `tools/test_build_gate.sh`, `tools/test_release_signing.sh`; each protection has a mutant that fails.
