#!/usr/bin/env bash
# Build one Witness-Coop-Setup.exe (32-bit, so it runs everywhere).
#   tools/build_setup.sh <out.exe> <version>                          the thin Setup of the zip (runs the scripts beside it)
#   tools/build_setup.sh <out.exe> <version> <payload.bin> [flags]    the one-file setup: payload.bin (tools/pack_payload.py) embedded, its SHA-256 compiled in
# flags: extra compiler flags (the tests build mutants with -DSETUP_MUT_NO_HASH_CHECK and friends, or force a wrong hash with -DPAYLOAD_SHA256_OVERRIDE=...).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$1"; ver="$2"; payload="${3:-}"; shift 3 2>/dev/null || shift $#
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
numeric="$(echo "$ver" | sed -n 's/^v\?\([0-9]\+\)\.\([0-9]\+\)\.\([0-9]\+\).*/\1,\2,\3,0/p')"; numeric="${numeric:-0,0,0,0}"
{
    echo '#include <winver.h>'
    echo "1 24 \"$here/dist/installer.manifest\""
    echo "1 ICON \"$here/dist/installer.ico\""
    echo "1 VERSIONINFO FILEVERSION $numeric PRODUCTVERSION $numeric FILEOS 0x40004 FILETYPE 1 {"
    echo "  BLOCK \"StringFileInfo\" { BLOCK \"040904b0\" { VALUE \"ProductName\", \"The Witness Co-op\" VALUE \"FileDescription\", \"The Witness Co-op Setup\" VALUE \"ProductVersion\", \"$ver\" VALUE \"FileVersion\", \"$ver\" VALUE \"OriginalFilename\", \"Witness-Coop-Setup.exe\" VALUE \"LegalCopyright\", \"MIT licence\" } }"
    echo '  BLOCK "VarFileInfo" { VALUE "Translation", 0x409, 1200 } }'
    [ -n "$payload" ] && echo "100 RCDATA \"$payload\""
} > "$tmp/setup.rc"
i686-w64-mingw32-windres -I "$here/dist" "$tmp/setup.rc" -o "$tmp/setup-res.o"
flags=(-Os -Wall -Wextra -mwindows -D_WIN32_WINNT=0x0601 -DSETUP_VERSION="\"$ver\"")
if [ -n "$payload" ]; then
    sha="$(sha256sum "$payload" | cut -d' ' -f1)"
    flags+=(-DHAVE_PAYLOAD -DPAYLOAD_SHA256="\"$sha\"")
fi
i686-w64-mingw32-gcc "${flags[@]}" "$@" -o "$out" "$here/dist/installer.c" "$tmp/setup-res.o" -lshell32
