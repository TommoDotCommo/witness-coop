#!/bin/bash
# Restart both test copies with the current proxy, fresh new games, windows side by side.
# CFG_A / CFG_B add lines to each copy's witness_coop.cfg (test-only switches: drop_first_event=1, no_resend=1, no_echo_guard=1).
. "$(dirname "$0")/config.sh"
for p in A B; do [ -f "$RUN/pid$p.txt" ] && taskkill.exe /PID "$(cat "$RUN/pid$p.txt")" /F >/dev/null 2>&1; done; sleep 4
"$TOOLS/install.sh" >/dev/null || exit 1
printf 'id=1\nlisten=47001\npeer=47002\n%s\n' "$CFG_A" > "$WITNESS_DIR1/witness_coop.cfg"
printf 'id=2\nlisten=47002\npeer=47001\n%s\n' "$CFG_B" > "$WITNESS_DIR2/witness_coop.cfg"
rm -f "$WITNESS_DIR1/witness_proxy.log" "$WITNESS_DIR2/witness_proxy.log" "$WITNESS_DIR1/witness_cmd.txt" "$WITNESS_DIR2/witness_cmd.txt"
launch() {   # dir -> pid; the game opens an options dialog unless -skip_config_dialog is given
  timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$1")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r'; }
PA=$(launch "$WITNESS_DIR1"); echo "$PA" > "$RUN/pidA.txt"
for i in $(seq 1 45); do grep -q "overlay: resources ready" "$WITNESS_DIR1/witness_proxy.log" 2>/dev/null && break; sleep 2; done
PB=$(launch "$WITNESS_DIR2"); echo "$PB" > "$RUN/pidB.txt"
for i in $(seq 1 45); do grep -q "overlay: resources ready" "$WITNESS_DIR2/witness_proxy.log" 2>/dev/null && break; sleep 2; done
timeout 20 "$PYW" -I "$(win "$TOOLS/win/wplace.py")" "$PA" 0 0 1270 760 >/dev/null
timeout 20 "$PYW" -I "$(win "$TOOLS/win/wplace.py")" "$PB" 1280 0 1270 760 >/dev/null
echo "A=$PA B=$PB"
