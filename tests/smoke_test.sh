#!/bin/sh
set -eu

PORT="${PORT:-5599}"
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SERVER_OUT="/tmp/tc_smoke_server_${PORT}.out"
SERVER_ERR="/tmp/tc_smoke_server_${PORT}.err"
ALICE_OUT="/tmp/tc_smoke_alice_${PORT}.out"
BOB_OUT="/tmp/tc_smoke_bob_${PORT}.out"

rm -f "$SERVER_OUT" "$SERVER_ERR" "$ALICE_OUT" "$BOB_OUT"

"$ROOT_DIR/bin/server" "$PORT" 10 2 >"$SERVER_OUT" 2>"$SERVER_ERR" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1

{
    printf 'register alice pass1\n'
    sleep 0.2
    printf 'login alice pass1\n'
    sleep 1
    printf 'users\n'
    sleep 0.2
    printf 'quit\n'
} | "$ROOT_DIR/bin/client" 127.0.0.1 "$PORT" >"$ALICE_OUT"

{
    printf 'register bob pass2\n'
    sleep 0.2
    printf 'login bob pass2\n'
    sleep 0.2
    printf 'global\n'
    sleep 0.2
    printf 'quit\n'
} | "$ROOT_DIR/bin/client" 127.0.0.1 "$PORT" >"$BOB_OUT"

sleep 1
cleanup
trap - EXIT INT TERM

grep -q 'Registrazione completata' "$ALICE_OUT"
grep -q 'Login effettuato' "$ALICE_OUT"
grep -q 'Utenti collegati:' "$ALICE_OUT"
grep -q 'Registrazione completata' "$BOB_OUT"
grep -q 'Login effettuato' "$BOB_OUT"
grep -q 'Mappa globale proprieta' "$BOB_OUT"

if [ "$(wc -c < "$SERVER_OUT")" -ne 0 ]; then
    echo "Errore: il server ha scritto su stdout" >&2
    cat "$SERVER_OUT" >&2
    exit 1
fi

echo "Smoke test OK"
