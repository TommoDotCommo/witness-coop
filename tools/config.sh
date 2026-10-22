# Sourced by the other scripts. Override with environment variables.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$ROOT/tools"
WITNESS_DIR1="${WITNESS_DIR1:-$ROOT/local/witness-1}"     # two disposable copies of the game install
WITNESS_DIR2="${WITNESS_DIR2:-$ROOT/local/witness-2}"
PYW="${PYW:-/mnt/c/Python311/python.exe}"                    # Windows Python 3 (PowerShell hangs under this WSL setup)
RUN="$ROOT/local/run"; mkdir -p "$RUN"                       # pid files, screenshots
EXE_SHA256=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5                                               # the only supported build: Steam 11370412
ANSEL_SHA256=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15
win() { wslpath -w "$1"; }
pyw() { "$PYW" -I "$@"; }
