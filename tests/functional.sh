#!/bin/sh
# Functional tests — drive the CLI from the outside.
#
# Requires: ./bitchat-linux and tests/build/make_fixture to exist.
# Usage:    tests/functional.sh

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="$ROOT/bitchat-linux"
FX="$ROOT/tests/build/make_fixture"

if [ ! -x "$CLI" ]; then
    echo "missing $CLI — run 'make' first" >&2; exit 2
fi
if [ ! -x "$FX" ]; then
    echo "missing $FX — run 'make functional' first" >&2; exit 2
fi

PASS=0
FAIL=0

# run_case <name> <fixture> <egrep-pattern>
run_case() {
    name="$1"; fixture="$2"; pattern="$3"
    out=$("$FX" "$fixture" | "$CLI" --decode 2>&1) || {
        echo "  FAIL $name: --decode returned nonzero" >&2
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

# self-test command
out=$("$CLI" --self-test 2>&1)
if printf '%s\n' "$out" | grep -q "self-test: OK"; then
    echo "  ok  self_test"; PASS=$((PASS+1))
else
    echo "  FAIL self_test" >&2; FAIL=$((FAIL+1))
fi

run_case announce_text       announce   'nickname="russell"'
run_case announce_noise_key  announce   'noise_pubkey=101112131415'
run_case announce_type       announce   'type.*announce'
run_case message_text        message    'text.*: hello from the mesh'
run_case padded_strips       padded     'nickname="russell"'
run_case v2_header           v2_routed  'BitchatPacket v2'
run_case v2_route_hops       v2_routed  'route.*2 hops'

# error paths
out=$(echo "zzznot hex" | "$CLI" --decode 2>&1 || true)
if printf '%s\n' "$out" | grep -q "invalid hex"; then
    echo "  ok  rejects_garbage_hex"; PASS=$((PASS+1))
else
    echo "  FAIL rejects_garbage_hex" >&2; FAIL=$((FAIL+1))
fi

out=$(echo "01 02 03 04" | "$CLI" --decode 2>&1 || true)
if printf '%s\n' "$out" | grep -q "decode failed"; then
    echo "  ok  rejects_short_frame"; PASS=$((PASS+1))
else
    echo "  FAIL rejects_short_frame" >&2; FAIL=$((FAIL+1))
fi

echo "## functional.sh — $PASS/$((PASS+FAIL)) passed"
[ "$FAIL" -eq 0 ]
