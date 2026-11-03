# The launcher's updater: what a release must look like

The launcher (`launcher/update.h`, `update_win.h`) offers an update only for a GitHub release that passes every rule below. A release that fails one is never offered (the check is silent;
the reason is in the launcher's log as `update: check failed, code N`). Nothing here needs a server of ours: GitHub's public release list is the whole interface.

**Mod managers.** When the managed files have more than one hard link and no Setup receipt (Vortex deploys that way), the launcher does not download or replace anything: the banner and the About block say "Installed by Vortex: update it in Vortex" and the Update buttons are hidden (`lu_owner` in `launcher/update.h`; `LU_ERR_MANAGER`). Files copied by hand have no receipt either: run Setup once and it adopts them.

## Publishing

1. `tools/release.sh v0.x.y` (signed if `SIGN_CMD` is set, see SIGNING.md). It writes `local/release/witness-coop-v0.x.y.zip` and `witness-coop-v0.x.y.zip.sha256`, and
   `witness-coop-v0.x.y-single/Witness-Coop-Setup.exe` (+ `.sha256`).
2. `gh release create v0.x.y --title ... --notes-file docs/RELEASE-NOTES-v0.x.y.md [--prerelease] <the zip> <the zip's .sha256> <Setup.exe> <Setup.exe's .sha256>` (ask the owner first: it is public).

## The rules the updater applies

| Rule | Why |
|---|---|
| The tag is a semantic version, optionally with a leading `v`: `v0.2.0`, `v0.2.0-beta.1` (letters, digits, `.`, `_`, `+`, `-`; at most 47 characters). | Anything else is ignored (`LU_ERR_VERSION`). Pre-release identifiers are compared as semver does (`beta.10` is newer than `beta.9`). |
| The release is not a draft. Pre-releases are offered only to an installed pre-release (a tag with `-beta.1` and the like) or when the player sets `update_prereleases=1`. | Players on a plain `v0.2.0` are not moved onto betas. **Tag the beta releases with a pre-release part (`v0.3.0-beta.1`) and mark them as pre-release on GitHub**, so beta testers get them and others do not. |
| The release has an asset named exactly `witness-coop-<tag>.zip` (the tag as in `tag_name`, with its `v`) of at most 64 MB. | `release.sh` produces this name from the version you give it: give it the same string as the tag. |
| The release has an asset `witness-coop-<tag>.zip.sha256` of at most 4 KB, in `sha256sum` form: `<64 hex>  witness-coop-<tag>.zip`. | The updater verifies the download against it and refuses a release without it. (This protects against a damaged or truncated download, not against a replaced release: that is what code signing is for.) |
| The archive holds, under `witness-coop-<tag>/`, `dsound.dll`, `witness_launcher.exe` (both 64-bit Windows executables), `witness_models/adventurer.glb` and `witness_models/adventurer.glb.txt`. No entry may have an absolute path, `..`, a backslash or a drive letter. | These are the files Setup manages (`dist/install-state.ps1`: `$script:WitnessManaged`). If a release adds a managed file, add it to `lu_targets` in `launcher/update.h` and to the tests in the same commit. |
| The tag is newer than the installed version. | An equal or older release is never installed (no downgrade). |

## What the updater does to a player's folder

It goes through the receipt rules of the installer, so a copy the installer does not own is left alone: see README, *Updating*. In short: only files whose `.coop-receipt.json` matches are
replaced, all or none; no file of the game is replaced (a folder in the v0.1.0-alpha layout is migrated: `dsound.dll` added, the game's own `AnselSDK64.dll` put back after its SHA-256 is checked); the proxy only while the game is not running; the old launcher stays as `witness_launcher.exe.coop-old` until the next start.
`witness_update.state` (a journal) exists only while an update is in progress; if it is found at start the update is rolled back.

## Testing a release candidate without publishing it

`python3 tools/test_launcher_update_win.py` builds the Windows updater and runs it against a local fake release server (no GitHub call): it serves releases made by the same
package builder the tests use. To try a real package, put its zip and `.sha256` into a local server (any static file server) and set `update_api_url=http://127.0.0.1:<port>/releases` in the test
folder's `witness_launcher_update.cfg` (only `https://` and `http://127.0.0.1`/`localhost` are accepted). `tools/qa_installer.sh` checks the package inventory.
