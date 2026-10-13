#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#
# Two-computer per-monitor routing simulation.
#
# Runs a real deskflow-core server and client (protocol 1.9, X11 backends) on two
# Xvfb displays with RandR monitors, drives the server's pointer with xdotool and
# checks where the pointer lands on each display. This is a simulation: both
# "computers" are X servers in one container; no physical input devices.
#
#   server "mona-sim"  (:91, 4480x1440): M1 2560x1440+0+0, M2 1920x1080+2560+0
#   client "queen-sim" (:92, 3000x1920): Q1 1920x1080+0+0, Q2 1080x1920+1920+0 (portrait)
#
# Global layout (interleaved):   [M1][Q1][M2]
#                                    [Q2]
#
# usage: monitor-layout-sim.sh [layout|legacy|old-client|old-server]   (default: layout)
#   layout      new server + new client, per-monitor layout enabled
#   legacy      new server + new client, layout disabled (computer links only)
#   old-client  new server (layout enabled) + stock 1.27.0 client (protocol 1.8)
#   old-server  stock 1.27.0 server (protocol 1.8) + new client
# Expects the build in /build (see dev/docker/compose.yml) and runs in the dev image.
# The old-* modes need a stock v1.27.0 core in $LEGACY_CORE (see docs/dev/monitor-layout.md).

set -u
MODE=${1:-layout}
CORE=${CORE:-/build/bin/deskflow-core}
LEGACY_CORE=${LEGACY_CORE:-/build/legacy-build/bin/deskflow-core}
SERVER_CORE=$CORE
CLIENT_CORE=$CORE
[ "$MODE" = old-client ] && CLIENT_CORE=$LEGACY_CORE
[ "$MODE" = old-server ] && SERVER_CORE=$LEGACY_CORE
WORK=${WORK:-/tmp/deskflow-sim}
PORT=24811
rm -rf "$WORK"
mkdir -p "$WORK"

FAILURES=0
pass() { echo "PASS: $*"; }
fail() {
  echo "FAIL: $*"
  FAILURES=$((FAILURES + 1))
}

cleanup() {
  kill "${PIDS[@]}" 2>/dev/null
  wait 2>/dev/null
}
PIDS=()
trap cleanup EXIT

# --- displays -------------------------------------------------------------
gcc -O1 -o "$WORK/fake-monitors" "$(dirname "$0")/fake-monitors.c" -lXrandr -lX11 || exit 1
Xvfb :91 -screen 0 4480x1440x24 -nolisten tcp 2>/dev/null &
PIDS+=($!)
Xvfb :92 -screen 0 3000x1920x24 -nolisten tcp 2>/dev/null &
PIDS+=($!)
sleep 1.5
DISPLAY=:91 "$WORK/fake-monitors" M1=2560x1440+0+0 M2=1920x1080+2560+0 >/dev/null &
PIDS+=($!)
DISPLAY=:92 "$WORK/fake-monitors" Q1=1920x1080+0+0 Q2=1080x1920+1920+0 >/dev/null &
PIDS+=($!)
sleep 0.5
echo "server monitors:"; DISPLAY=:91 xrandr --listmonitors | sed 's/^/  /'
echo "client monitors:"; DISPLAY=:92 xrandr --listmonitors | sed 's/^/  /'

# --- configuration --------------------------------------------------------
[ "$MODE" = legacy ] && LAYOUT=false || LAYOUT=true
cat >"$WORK/server.conf" <<EOF
section: links
	mona-sim:
		right = queen-sim
	queen-sim:
		left = mona-sim
end
section: options
end
EOF

cat >"$WORK/server.ini" <<EOF
[core]
computerName=mona-sim
port=$PORT
display=:91
[log]
level=${SIM_LOG_LEVEL:-Debug}
[security]
tlsEnabled=false
[server]
externalConfig=true
externalConfigFile=$WORK/server.conf
monitorLayout=$LAYOUT
[computer_mona-sim]
name=mona-sim
monitors="id=M1;x=0;y=0;w=2560;h=1440;gx=0;gy=0", "id=M2;x=2560;y=0;w=1920;h=1080;gx=4480;gy=0"
[computer_queen-sim]
name=queen-sim
monitors="id=Q1;x=0;y=0;w=1920;h=1080;gx=2560;gy=0", "id=Q2;x=1920;y=0;w=1080;h=1920;gx=2560;gy=1080"
EOF

cat >"$WORK/client.ini" <<EOF
[core]
computerName=queen-sim
port=$PORT
display=:92
[log]
level=${SIM_LOG_LEVEL:-Debug}
[security]
tlsEnabled=false
[client]
remoteHost=127.0.0.1
EOF

start_client() {
  HOME="$WORK/client-home" DISPLAY=:92 "$CLIENT_CORE" client --new-instance -s "$WORK/client.ini" >>"$WORK/client.log" 2>&1 &
  CLIENT_PID=$!
  PIDS+=($CLIENT_PID)
}

mkdir -p "$WORK/server-home" "$WORK/client-home"
HOME="$WORK/server-home" DISPLAY=:91 "$SERVER_CORE" server --new-instance -s "$WORK/server.ini" >"$WORK/server.log" 2>&1 &
PIDS+=($!)
sleep 2
start_client

for _ in $(seq 1 40); do
  grep -q 'client "queen-sim" has connected' "$WORK/server.log" && break
  sleep 0.25
done
if grep -q 'client "queen-sim" has connected' "$WORK/server.log"; then
  pass "client connected"
else
  fail "client did not connect"
  tail -20 "$WORK/server.log" "$WORK/client.log"
  exit 1
fi
sleep 0.5

# --- helpers ----------------------------------------------------------------
srv() { DISPLAY=:91 xdotool "$@"; }
# relative pointer motion on the server, split into steps of at most 200px like a real mouse
# (one big XTest jump would be clamped at the server's screen edge before deskflow re-centres it)
move() {
  local dx=$1 dy=$2 sx sy
  while [ "$dx" != 0 ] || [ "$dy" != 0 ]; do
    sx=$((dx > 200 ? 200 : (dx < -200 ? -200 : dx)))
    sy=$((dy > 200 ? 200 : (dy < -200 ? -200 : dy)))
    srv mousemove_relative -- "$sx" "$sy"
    dx=$((dx - sx))
    dy=$((dy - sy))
    sleep 0.1
  done
}
pos() { DISPLAY=$1 xdotool getmouselocation | sed -E 's/x:([-0-9]+) y:([-0-9]+).*/\1 \2/'; }
settle() { sleep 0.4; }
active() { grep -o 'switch from "[^"]*" to "[^"]*" at [-0-9]*,[-0-9]*' "$WORK/server.log" | tail -1; }
switches() { grep -c 'switch from' "$WORK/server.log"; }

expect_client_at() {
  local want="$1 $2" got
  got=$(pos :92)
  [ "$got" = "$want" ] && pass "$3 -> queen-sim pointer at $got" || fail "$3 -> queen-sim pointer at $got, expected $want"
}

expect_switch() {
  local want=$1 got
  got=$(active)
  [[ "$got" == *"$want"* ]] && pass "$2 -> $got" || fail "$2 -> last switch '$got', expected '$want'"
}

# --- scenarios ----------------------------------------------------------------
# Only relative moves are used: on Xvfb, XTest absolute moves do not produce XI2 raw
# motion, so the server would not see them (a real mouse only produces relative motion).
# The server's pointer starts at the centre of its desktop, 2240,720.
if [ "$MODE" = layout ]; then
  grep -q 'per-monitor layout enabled, 4 monitor placement' "$WORK/server.log" &&
    pass "server loaded 4 placements" || fail "server did not load placements"
  grep -q 'computer "queen-sim" has 2 monitor(s), routed per monitor' "$WORK/server.log" &&
    pass "client reported 2 monitors over DMON" || fail "client monitors not received"

  # 1. M1 -> right -> Q1. The server's OS has M2 to the right of M1; the layout puts Q1 there.
  move 300 -20; settle
  [ "$(switches)" = 0 ] && pass "1a moving inside M1 stays on mona-sim" || fail "1a unexpected switch: $(active)"
  move 25 0; settle
  expect_switch 'to "queen-sim" at 0,700' "1b M1 right edge (inner OS edge) crosses to Q1"
  expect_client_at 0 700 "1b"

  # 2. ordinary motion on the client
  move 400 0; settle
  expect_client_at 400 700 "2 relative move on Q1"

  # 3. Q1 -> right -> M2 on the server, although queen's OS has Q2 there
  move 1600 0; settle
  expect_switch 'to "mona-sim" at 2561,700' "3 Q1 right edge crosses to M2"
  got=$(pos :91)
  [ "$got" = "2561 700" ] && pass "3 -> mona-sim pointer at $got" || fail "3 -> mona-sim pointer at $got, expected 2561 700"

  # 4. M2 -> left -> Q1 (the server's OS has M1 there), then Q1 -> down -> portrait Q2
  move -6 0; settle
  expect_switch 'to "queen-sim" at 1919,700' "4a M2 left edge (inner OS edge) crosses to Q1"
  move -1419 300; settle
  expect_client_at 500 1000 "4b move on Q1"
  move 0 100; settle
  expect_client_at 2420 1 "4c Q1 bottom edge -> Q2 top (layout, not queen's OS arrangement)"

  # 5. Q2's left edge is only partly shared with M1 (M1 is 1440 tall): below that it's a wall
  move 0 1500; settle
  expect_client_at 2420 1501 "5a move down Q2"
  before=$(switches)
  move -700 0; settle
  expect_client_at 1920 1501 "5b Q2 left edge below M1 is a wall"
  [ "$(switches)" = "$before" ] && pass "5b no switch at the wall" || fail "5b unexpected switch: $(active)"
  move 0 -1300; settle
  move -50 0; settle
  expect_switch 'to "mona-sim" at 2558,1281' "5c Q2 left edge alongside M1 crosses to M1"

  # 6. reconnect: restart the client; routing must resume with the same placements.
  #    M1's right edge below Q1 (y >= 1080 globally) touches Q2.
  kill "$CLIENT_PID"; wait "$CLIENT_PID" 2>/dev/null
  sleep 1
  start_client
  for _ in $(seq 1 60); do
    [ "$(grep -c 'client "queen-sim" has connected' "$WORK/server.log")" -ge 2 ] && break
    sleep 0.25
  done
  [ "$(grep -c 'client "queen-sim" has connected' "$WORK/server.log")" -ge 2 ] &&
    pass "6a client reconnected" || fail "6a client did not reconnect"
  sleep 0.5
  move 10 0; settle
  expect_switch 'to "queen-sim" at 1920,201' "6b after reconnect M1 (below Q1) -> Q2"
  expect_client_at 1920 201 "6b"
elif [ "$MODE" = old-client ]; then
  grep -q 'computer "queen-sim" has 1 monitor(s), using computer links' "$WORK/server.log" &&
    pass "1.8 client is treated as one whole-desktop monitor" || fail "1.8 client topology unexpected"
  grep -q 'received .* monitor(s)' "$WORK/server.log" && fail "1.8 client sent DMON" || pass "no DMON from 1.8 client"

  # 1. the layout has nothing for the unplaced client: M1 -> M2 stays local
  move 300 -20; settle
  move 25 0; settle
  [ "$(switches)" = 0 ] && pass "1 M1 -> M2 stays local" || fail "1 unexpected switch $(active)"

  # 2. the legacy link on the desktop's right edge still reaches the 1.8 client
  move 1914 0; settle
  expect_switch 'to "queen-sim" at' "2 desktop right edge reaches the 1.8 client via its legacy link"

  # 3. and the legacy link back
  move -400 0; settle
  expect_switch 'to "mona-sim" at' "3 back from the 1.8 client via its legacy link"
elif [ "$MODE" = old-server ]; then
  grep -q 'protocol v1.8' "$WORK/server.log" && pass "server is protocol 1.8" || fail "server is not the stock 1.8 build"
  grep -qi 'hello back with version .* 1.8' "$WORK/client.log" &&
    pass "new client negotiated down to 1.8" || fail "client did not negotiate 1.8"

  move 300 -20; settle
  move 25 0; settle
  move 1914 0; settle
  expect_switch 'to "queen-sim" at' "1 1.8 server reaches the new client via its legacy link"
  grep -q 'monitorLayout' "$WORK/server.ini" && pass "settings: monitorLayout kept" ||
    pass "settings: 1.27.0 dropped server/monitorLayout on load (expected; layout off after re-upgrade)"
  grep -q 'id=M1' "$WORK/server.ini" && pass "settings: monitor placements survive a 1.27.0 run" ||
    fail "settings: monitor placements were lost"
else
  grep -q 'per-monitor layout disabled' "$WORK/server.log" &&
    pass "legacy mode: per-monitor layout disabled" || fail "layout unexpectedly enabled"

  # 1. legacy: crossing M1 -> M2 stays on the server
  move 300 -20; settle
  move 25 0; settle
  [ "$(switches)" = 0 ] && pass "1 legacy: M1 -> M2 stays local" || fail "1 legacy: unexpected switch $(active)"

  # 2. legacy: only the right edge of the whole desktop leads to the client
  move 1914 0; settle
  expect_switch 'to "queen-sim" at' "2 legacy: desktop right edge crosses to queen-sim"
fi

echo
echo "--- server log (routing lines) ---"
grep -E 'monitor|switch from|per-monitor|routed|computer links' "$WORK/server.log" | sed -E 's/^\[[^]]*\] //' | grep -v '^\s*/' | tail -40
echo
if [ "$FAILURES" = 0 ]; then
  echo "RESULT: all $MODE scenarios passed"
else
  echo "RESULT: $FAILURES failure(s)"
fi
exit "$FAILURES"
