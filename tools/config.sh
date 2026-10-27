# Sourced by the other scripts. Override with environment variables.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$ROOT/tools"
# Optional private environment and external resource provider. No workstation defaults.
[ ! -f "$ROOT/local/env.sh" ] || . "$ROOT/local/env.sh"
WITNESS_RESOURCES="${WITNESS_RESOURCES:-}"
if [ -n "$WITNESS_RESOURCES" ]; then
  [ -x "$WITNESS_RESOURCES" ] || { echo "configured resource provider is not executable" >&2; return 2; }
  export WITNESS_RESOURCE_OWNER="${WITNESS_RESOURCE_OWNER:-witness-$(python3 -c 'import uuid; print(uuid.uuid4().hex)')}"
  if witness_exports=$("$WITNESS_RESOURCES" env witness 2>/dev/null); then
    eval "$witness_exports"
  elif [ "${WITNESS_ALLOCATING:-0}" != 1 ]; then
    echo "no allocation for this owner; acquire resources before installing or starting games" >&2
    return 3
  fi
fi
WITNESS_DIR1="${WITNESS_DIR1:-$ROOT/local/witness-1}"     # disposable game copies
WITNESS_DIR2="${WITNESS_DIR2:-$ROOT/local/witness-2}"
WITNESS_PORT1="${WITNESS_PORT1:-47001}"; WITNESS_PORT2="${WITNESS_PORT2:-47002}"
witness_slots="${WITNESS_SLOTS:-}"; SLOT1="${witness_slots%%,*}"; SLOT2="${witness_slots##*,}"; SLOT1="${SLOT1:-0}"; SLOT2="${SLOT2:-1}"
PYW="${PYW:-python.exe}"                    # Windows Python 3; set PYW to its executable
RUN="${WITNESS_RUN:-$ROOT/local/run}"; mkdir -p "$RUN"           # pid files, screenshots (per agent in resource provider mode)
EXE_SHA256=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5                                               # the only supported build: Steam 11370412
ANSEL_SHA256=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15
win() { wslpath -w "$1"; }
pyw() { "$PYW" -I "$@"; }

export WITNESS_RESOURCES PYW WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2
export WITNESS_RUN="$RUN"
