# Draft release-note lines: a package mod managers can handle, and a Setup that finds everything (not tagged, not published)

- **Nothing is written into the game folder while you play.** Your co-op settings (`witness_coop.cfg`), the mod's log and the update schedule live in
  `%LOCALAPPDATA%\witness-coop\`. A `witness_coop.cfg` left beside the exe by an earlier version is moved there once. Test tools can still put a
  `witness_coop.cfg` with an instance key (`save_dir`, `force_active`, `run_unfocused`, `diag_dir`) beside the exe as an explicit per-instance override.
- **A package for mod managers.** `coop-mod.json` (the game, the supported build and its SHA-256, the proxy, the Proton override, every file with its SHA-256) is in
  the zip, and every `.dll` and `.exe` has a version resource. Copying the files into the game folder is a complete install and deleting them a complete uninstall.
  A copy note for the identical zip on Nexus Mods is written by the release script (`NEXUS-<version>.txt`).
- **Setup knows who installed it.** Vortex's linked files are left to Vortex ("Installed by Vortex: update or remove it there"; Play still works; the launcher shows
  the same instead of updating). A release copied in by hand is adopted by Setup and updated normally.
- **Setup from a script.** `Witness-Coop-Setup.exe status [--json]`, `install <folder> --yes`, `uninstall <folder> --yes` with exit codes 0 done, 1 failed, 2 usage,
  3 unsupported build, 4 game running (the same in `install-linux.sh`).
- **A Setup that finds every install.** Steam libraries, GOG and Epic installs are cards with a build check; options for a desktop shortcut, the Windows Firewall rule
  *The Witness Co-op (UDP)* (game and launcher, Private and Public, one administrator prompt) and *Open invite links in the launcher* (`witness-coop://`).
  Uninstall removes all three. The last screen offers Play.

Checks (game-free): `make -C common userdata-test`, `python3 -I tools/test_mod_manifest.py`, `tools/test_setup_options.ps1 -Mutants`, `tools/test_setup_verbs.ps1 -Mutants`,
`tools/test_install_linux.sh`, `python3 -I tools/test_launcher_update.py`, `make -C launcher test`, `tools/qa_installer.sh`. Live (one slot): `tools/userdata_live.sh`.
