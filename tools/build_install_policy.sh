#!/usr/bin/env bash
# Build the 32-bit setup policy helper (works from either PowerShell architecture).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
resource=$(mktemp --suffix=.o)
trap 'rm -f "$resource"' EXIT
# An explicit execution level stops Windows' installer-name heuristics from demanding elevation for this read-only helper.
# MOD_VERSION (tools/release.sh passes it) goes into the file's VERSIONINFO beside the manifest
rc=$(mktemp --suffix=.rc); trap 'rm -f "$resource" "$rc"' EXIT
python3 -I "$root/tools/versioninfo.py" "${MOD_VERSION:-dev}" install-policy.exe "The Witness Co-op Setup helper" "$rc.ver"
{ cat "$root/dist/install-policy.rc"; cat "$rc.ver"; } > "$rc"; rm -f "$rc.ver"
(cd "$root/dist" && i686-w64-mingw32-windres "$rc" -o "$resource")
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -o "${1:-$root/dist/install-policy.exe}" "$root/dist/install-policy.c" "$resource"
