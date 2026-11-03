# Code signing and Windows false positives

The downloads are programs that unpack files, replace a DLL that a game loads and start PowerShell. That is what malware also does, so
Windows SmartScreen and antivirus programs are suspicious of an unsigned build. Signing every `.exe` and `.dll` with a code-signing
certificate gives them a publisher to trust and lets the download build a reputation.

## What is ready

`tools/release.sh` signs **in place, before anything is hashed or embedded**: the proxy `dsound.dll`, `witness_launcher.exe`,
`install-policy.exe`, the zip's thin `Witness-Coop-Setup.exe`; then it packs those into the one-file setup, signs that last, builds the zip and
hashes it. Afterwards `tools/pe_signature.py` confirms every signed file carries a certificate block, `SIGN_VERIFY_CMD` (if set) verifies it for real,
and `tools/check_release_package.py ... --require-signed` checks the zip, the one-file setup and the files inside its payload. A signer that misses a file, or
parts signed after they were embedded, fails the build (`tools/test_release_signing.sh` proves both with a stand-in signer).
Without `SIGN_CMD` the build prints a warning and is unsigned: fine for tests, not for a public release.

## Set it up

1. Get a code-signing certificate: an OV or EV certificate from a certificate authority, or a cloud signing service. An EV certificate (or a
   service that provides one) may shorten SmartScreen's warm-up; either kind builds reputation as the download is used. Check the vendor's current terms.
2. Put the signing tool on the machine that runs the release. Examples (the command is yours; `{}` is replaced by the file's path, `{win}` by its Windows path in WSL):

   ```sh
   # signtool (Windows SDK), certificate in the store, RFC 3161 timestamp so the signature outlives the certificate
   SIGN_CMD='signtool.exe sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /sha1 <thumbprint> {win}' \
   SIGN_VERIFY_CMD='signtool.exe verify /pa {win}' tools/release.sh v0.x.y

   # osslsigncode with a PFX file (the password from the environment, not the command line history)
   SIGN_CMD='osslsigncode sign -pkcs12 cert.pfx -pass "$PFX_PASS" -h sha256 -t http://timestamp.digicert.com -in {} -out {}.signed && mv {}.signed {}' tools/release.sh v0.x.y
   ```
3. Always timestamp. Never commit the certificate or its password. One certificate can sign this mod and other mods by the same publisher.

## If Windows or an antivirus still flags it (false positives)

Submit the signed files to the vendor so the detection is removed for everyone:

1. **Microsoft Defender / SmartScreen:** open the Microsoft Security Intelligence file submission page (search "Microsoft Security Intelligence submit a file"),
   sign in, choose *Software developer*, upload `Witness-Coop-Setup.exe` (and the zip if it is flagged), and say what the program is: a mod installer for the game
   The Witness, open source at the repository URL, that adds a `dsound.dll` to the game's folder and starts Windows PowerShell for its setup window. Pick *Incorrectly detected as malware/malicious*
   and give the detection name Defender reported. Keep the submission ID. Re-submit each new version until its reputation builds.
2. **SmartScreen reputation** ("Windows protected your PC") is per file hash and per publisher certificate. A signed file warns less over time as people run it; an EV certificate may shorten that.
3. **Other antivirus vendors:** most have a free false-positive form on their site; upload the same signed file with the same description. VirusTotal shows which engines flag it.
4. Say in the release notes which download to use and its SHA-256 (the `.sha256` files), so a player can check what they got.

The form's wording changes from time to time; the substance (signed file, honest description, detection name) does not.

## Why PowerShell is still started with `-ExecutionPolicy Bypass`

Windows' default policy refuses unsigned scripts, which would stop the setup window itself. The bypass applies to that one process and to scripts that this exe has just
unpacked from its own hash-checked payload (or that sit beside the thin Setup). Signing the scripts as well would not remove the need for the policy setting on a default Windows.
A native setup program with no PowerShell would remove both the setting and the PowerShell-from-an-unknown-program pattern antivirus programs dislike; it is the larger, later step.
