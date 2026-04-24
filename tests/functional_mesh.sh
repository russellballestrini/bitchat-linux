#!/bin/sh
# functional_mesh.sh — real BLE round-trip across two adapters.
#
#   adapter A (hci0): runs tests/fake_peer.py — registers a GATT server
#                     that advertises the testnet service UUID and pushes
#                     a canned announce frame via notify
#   adapter B (hci1): runs ./bitchat-linux --listen-test — scans, finds
#                     the peer, subscribes, decodes
#
# A single radio cannot scan its own advertisement (BLE dual-role on one
# physical adapter doesn't loopback). This test requires TWO adapters —
# e.g., the laptop's built-in + a USB BLE dongle, or two ThinkPads on a
# shared desk with one running fake_peer and the other running listen.
#
# On a single-adapter box the test exits 77 (TAP-style "skip") so CI can
# distinguish missing-hardware from real failures.
#
# Usage:
#   tests/functional_mesh.sh                  # uses PEER_HCI=hci0 LISTEN_HCI=hci1
#   PEER_HCI=hci1 LISTEN_HCI=hci0 tests/functional_mesh.sh

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="$ROOT/bitchat-linux"
FX="$ROOT/tests/build/make_fixture"
PEER="$ROOT/tests/fake_peer.py"

[ -x "$CLI" ]  || { echo "missing $CLI — run 'make'" >&2; exit 2; }
[ -x "$FX" ]   || { echo "missing $FX — run 'make functional-mesh'" >&2; exit 2; }
[ -f "$PEER" ] || { echo "missing $PEER" >&2; exit 2; }

# Count BlueZ adapters on the box.
adapters=$(busctl --system --json=short call org.bluez / \
           org.freedesktop.DBus.ObjectManager GetManagedObjects 2>/dev/null \
           | grep -oE '/org/bluez/hci[0-9]+' | sort -u)
adapter_count=$(printf '%s\n' "$adapters" | grep -c '^/org/bluez/hci' || true)

if [ "$adapter_count" -lt 2 ]; then
    cat >&2 <<EOF
== functional_mesh.sh — SKIPPED
Only $adapter_count BlueZ adapter(s) on this box. Real-BLE round-trip needs 2.
Plug in a USB BLE dongle (~\$5) or run fake_peer.py on a second machine.
Adapters found:
$adapters
EOF
    exit 77
fi

PEER_HCI="${PEER_HCI:-hci0}"
LISTEN_HCI="${LISTEN_HCI:-hci1}"
PEER_PATH="/org/bluez/${PEER_HCI}"
LISTEN_PATH="/org/bluez/${LISTEN_HCI}"

# Verify both paths exist.
for p in "$PEER_PATH" "$LISTEN_PATH"; do
    printf '%s\n' "$adapters" | grep -qx "$p" || {
        echo "adapter $p not present (adapters: $adapters)" >&2
        exit 2
    }
done

echo "== functional_mesh.sh"
echo "   peer:   $PEER_PATH  (fake_peer.py)"
echo "   listen: $LISTEN_PATH  (bitchat-linux --listen-test)"

PEER_LOG=$(mktemp)
LISTEN_LOG=$(mktemp)

cleanup() {
    [ -n "${PEER_PID:-}" ] && kill "$PEER_PID" 2>/dev/null || true
    [ -n "${LISTEN_PID:-}" ] && kill "$LISTEN_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Launch fake peer first so it's advertising when listener starts.
python3 -u "$PEER" --adapter "$PEER_PATH" --timeout 20 > "$PEER_LOG" 2>&1 &
PEER_PID=$!
sleep 2

# Run listener for 12 seconds, then kill.
"$CLI" --listen-test --adapter "$LISTEN_PATH" > "$LISTEN_LOG" 2>&1 &
LISTEN_PID=$!
sleep 12
kill "$LISTEN_PID" 2>/dev/null || true
wait "$LISTEN_PID" 2>/dev/null || true

echo "-- peer log --"
cat "$PEER_LOG"
echo "-- listener log (first 30) --"
head -30 "$LISTEN_LOG"

PASS=0; FAIL=0
check() {
    name="$1"; pattern="$2"; src="$3"
    if grep -Eq "$pattern" "$src"; then
        echo "  ok  $name"; PASS=$((PASS+1))
    else
        echo "  FAIL $name: pattern not found in $src: $pattern" >&2
        FAIL=$((FAIL+1))
    fi
}

check peer_registered  'GATT app registered'         "$PEER_LOG"
check peer_advertised  'advertisement registered'    "$PEER_LOG"
check peer_pushed_frame 'emitted [0-9]+-byte frame'   "$PEER_LOG"
check listener_discovered_device 'device: .*matches'  "$LISTEN_LOG"
check listener_subscribed 'char: .*matches'           "$LISTEN_LOG"
check listener_decoded_announce 'nickname="russell"' "$LISTEN_LOG"

rm -f "$PEER_LOG" "$LISTEN_LOG"

echo "## functional_mesh.sh — $PASS/$((PASS+FAIL)) passed"
[ "$FAIL" -eq 0 ]
