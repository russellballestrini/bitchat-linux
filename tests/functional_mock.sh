#!/bin/sh
# functional_mock.sh — same-box mock: canned frames through stdin into the
# real dispatch path. Exercises everything from bytes-arrived-from-radio
# downward (decode, decompression, TLV, announce extraction, printer)
# without needing a BLE adapter.
#
# Usage: tests/functional_mock.sh

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="$ROOT/bitchat-linux"
FX="$ROOT/tests/build/make_fixture"

[ -x "$CLI" ] || { echo "missing $CLI — run 'make'" >&2; exit 2; }
[ -x "$FX" ]  || { echo "missing $FX — run 'make functional-mock'" >&2; exit 2; }

PASS=0
FAIL=0

# one_frame <name> <fixture> <pattern>
one_frame() {
    name="$1"; fixture="$2"; pattern="$3"
    out=$("$FX" --stream "$fixture" | "$CLI" --listen-stream 2>&1) || {
        echo "  FAIL $name: --listen-stream returned nonzero" >&2
        FAIL=$((FAIL+1)); return
    }
    if printf '%s\n' "$out" | grep -Eq "$pattern"; then
        echo "  ok  $name"; PASS=$((PASS+1))
    else
        echo "  FAIL $name: pattern not found: $pattern" >&2
        printf '  output was:\n%s\n' "$out" >&2
        FAIL=$((FAIL+1))
    fi
}

# three_frames_in_a_row
three_in_a_row() {
    out=$({ "$FX" --stream announce; "$FX" --stream message; "$FX" --stream v2_routed; } \
          | "$CLI" --listen-stream 2>&1)
    ok=1
    printf '%s\n' "$out" | grep -q 'nickname="russell"'      || ok=0
    printf '%s\n' "$out" | grep -q 'hello from the mesh'     || ok=0
    printf '%s\n' "$out" | grep -q 'BitchatPacket v2'        || ok=0
    if [ "$ok" = 1 ]; then
        echo "  ok  three_frames_back_to_back"; PASS=$((PASS+1))
    else
        echo "  FAIL three_frames_back_to_back" >&2
        printf '  output was:\n%s\n' "$out" >&2
        FAIL=$((FAIL+1))
    fi
}

# bogus length header should be rejected
rejects_bogus_length() {
    # 4-byte length 0xFFFFFFFF — clearly bogus
    out=$(printf '\xff\xff\xff\xff' | "$CLI" --listen-stream 2>&1 || true)
    if printf '%s\n' "$out" | grep -q 'bogus frame length'; then
        echo "  ok  rejects_bogus_length"; PASS=$((PASS+1))
    else
        echo "  FAIL rejects_bogus_length" >&2; FAIL=$((FAIL+1))
    fi
}

one_frame announce_via_stream   announce   'nickname="russell"'
one_frame message_via_stream    message    'text.*: hello from the mesh'
one_frame padded_via_stream     padded     'nickname="russell"'
one_frame v2_routed_via_stream  v2_routed  'route.*2 hops'
three_in_a_row
rejects_bogus_length

echo "## functional_mock.sh — $PASS/$((PASS+FAIL)) passed"
[ "$FAIL" -eq 0 ]
