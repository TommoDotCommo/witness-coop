#!/bin/bash
# Restart test copies with the current proxy, fresh new games, windows side by side. Optional A/B restarts only that peer.
# force_active=1: each game believes its window is active (full speed, accepts injected input) without taking the focus or the mouse.
# Every copy gets its own save folder (save_dir=instance: <game folder>\appdata, so the owner's real saves are never touched) and a
# frame cap (WITNESS_FPS, default 30). WITNESS_HUMAN=1: for a person playing both games: no force_active (the real mouse and keyboard work, the unfocused game
# drops to ~10 fps), no focus guard, A on the first monitor and B on the second (WITNESS_RECT_A/B = "x y w h"). CFG_A / CFG_B add lines to each copy's witness_coop.cfg (test-only switches: drop_first_event=1, no_resend=1, no_echo_guard=1).
. "$(dirname "$0")/config.sh"
case "${1:-both}" in
  both) peers=(A B);;
  A|B) peers=("$1");;
  *) echo 'usage: restart.sh [A|B|both]' >&2; exit 2;;
esac
PREV_FG=$(timeout 20 "$PYW" -I "$(win "$TOOLS/win/wfocus.py")" save)      # who has the focus now (the game grabs it when it starts)
for p in "${peers[@]}"; do [ -f "$RUN/pid$p.txt" ] && taskkill.exe /PID "$(cat "$RUN/pid$p.txt")" /F >/dev/null 2>&1; done; sleep 4
"$TOOLS/install.sh" "${1:-both}" >/dev/null || exit 1
launch() {   # dir -> pid; the game opens an options dialog unless -skip_config_dialog is given
  timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$1")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r'; }
for p in "${peers[@]}"; do
  if [ "$p" = A ]; then d="$WITNESS_DIR1"; id=1; listen="$WITNESS_PORT1"; peer="$WITNESS_PORT2"; cfg="${CFG_A:-}";
  else d="$WITNESS_DIR2"; id=2; listen="$WITNESS_PORT2"; peer="$WITNESS_PORT1"; cfg="${CFG_B:-}"; fi
  printf 'id=%s\nlisten=%s\npeer=%s\nsave_dir=instance\nshare_zip=1\nforce_active=%s\ncap_fps=%s\ndev_menu=1\n%s\n' "$id" "$listen" "$peer" "$([ -n "${WITNESS_HUMAN:-}" ] && echo 0 || echo 1)" "${WITNESS_FPS:-30}" "$cfg" > "$d/witness_coop.cfg"
  rm -f "$d/witness_proxy.log" "$d/witness_cmd.txt"
  [ -d "$d/appdata/The Witness" ] && rm -f "$d/appdata/The Witness/"*.witness_campaign "$d/appdata/The Witness/"*.png   # this instance's own saves only: reloads must start from known saves
  game_pid=$(launch "$d"); echo "$game_pid" > "$RUN/pid$p.txt"
  [ -x "$LAB" ] && "$LAB" register-pid "$game_pid" >/dev/null 2>&1
  for i in $(seq 1 45); do grep -q "overlay: resources ready" "$d/witness_proxy.log" 2>/dev/null && break; sleep 2; done
done
PA=$(cat "$RUN/pidA.txt" 2>/dev/null); PB=$(cat "$RUN/pidB.txt" 2>/dev/null)     # a peer that was never started (late join) has no pid file
# every game is registered with the lab (only registered pids are ever killed for this agent) and placed on the second monitor
# without taking focus (tile by slot number; in a plain checkout without the lab: the first monitor)
for w in "A $PA $SLOT1" "B $PB $SLOT2"; do set -- $w
  [ -n "$2" ] || continue
  if [ -n "${WITNESS_HUMAN:-}" ]; then read -r x y ww hh <<< "$([ "$1" = A ] && echo "${WITNESS_RECT_A:-320 180 1920 1080}" || echo "${WITNESS_RECT_B:-2880 180 1920 1080}")"
  elif [ -x "$LAB" ]; then read -r x y ww hh < <("$LAB" rect "$3"); else x=$(( ($3) * 1280 )); y=0; ww=1270; hh=760; fi
  timeout 20 "$PYW" -I "$(win "$TOOLS/win/wplace.py")" "$2" "$x" "$y" "$ww" "$hh" >/dev/null
done
# keep the focus where it was for up to 3 minutes (the game takes the foreground by itself while it loads); the bot
# focuses game A on purpose later, after this guard has ended
[ -n "${WITNESS_HUMAN:-}" ] || {
rm -f "$RUN/guard.stop"       # whoever starts a bot creates this file first, so the guard never fights it
( timeout 200 "$PYW" -I "$(win "$TOOLS/win/wfocus.py")" guard "$PREV_FG" 180 "$(win "$RUN/guard.stop")" $PA $PB >/dev/null 2>&1 & )
}
echo "A=$PA B=$PB"
