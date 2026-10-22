# Sourced by the other scripts. Override with environment variables.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$ROOT/tools"
# Lab mode: when the shared lab (~/source/coop-lab, see its LAB.md) has a lease for this agent, the instance folders, UDP ports,
# run folder and window tiles come from it, so several agents can run at once. Without a lease the older single-agent
# defaults below are used (two copies under local/).
LAB="${LAB:-$HOME/source/coop-lab/lab}"
if [ -z "${WITNESS_DIR1:-}" ] && [ -x "$LAB" ]; then eval "$("$LAB" env witness 2>/dev/null)" || true; fi
WITNESS_DIR1="${WITNESS_DIR1:-$ROOT/local/witness-1}"     # two disposable copies of the game install
WITNESS_DIR2="${WITNESS_DIR2:-$ROOT/local/witness-2}"
WITNESS_PORT1="${WITNESS_PORT1:-47001}"; WITNESS_PORT2="${WITNESS_PORT2:-47002}"
SLOT1="${LAB_SLOTS%%,*}"; SLOT2="${LAB_SLOTS##*,}"; SLOT1="${SLOT1:-0}"; SLOT2="${SLOT2:-1}"
PYW="${PYW:-/mnt/c/Python311/python.exe}"                    # Windows Python 3 (PowerShell hangs under this WSL setup)
RUN="${WITNESS_RUN:-$ROOT/local/run}"; mkdir -p "$RUN"           # pid files, screenshots (per agent in lab mode)
EXE_SHA256=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5                                               # the only supported build: Steam 11370412
ANSEL_SHA256=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15
win() { wslpath -w "$1"; }
pyw() { "$PYW" -I "$@"; }
