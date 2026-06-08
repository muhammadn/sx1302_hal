#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_MODE="stub"
SKIP_DAEMON=0
DEV_MODE=0

SOCKET_PATH="${MESHTASTIC_IPC_SOCKET:-/tmp/meshtastic-sx1302.sock}"
SOCKET_WAIT_SECONDS="${SOCKET_WAIT_SECONDS:-2}"
DAEMON_BIN="${DAEMON_BIN:-$SCRIPT_DIR/meshtasticd}"
STUB_BIN="${STUB_BIN:-$SCRIPT_DIR/meshtastic_runtime_stub}"
NATIVE_BIN="${NATIVE_BIN:-$SCRIPT_DIR/firmware/.pio/build/native/meshtasticd}"

usage() {
    cat <<'EOF'
Usage: run_ipc_stack.sh [--stub|--native|--dev] [--skip-daemon]

Options:
  --stub         Use local test runtime stub (default)
  --native       Use firmware native runtime binary
  --dev          Development machine mode (equivalent to --native --skip-daemon)
  --skip-daemon  Start runtime only (useful for IPC smoke checks)
  -h, --help     Show this help

Environment overrides:
  MESHTASTIC_IPC_SOCKET   Unix socket path (default: /tmp/meshtastic-sx1302.sock)
  MESHTASTIC_IPC_REQUIRED Set to 1 to make daemon fail-fast when runtime is missing
  SOCKET_WAIT_SECONDS     Runtime socket wait timeout (default: 2)
  DAEMON_BIN              sx1302 daemon path (default: ./meshtasticd)
  STUB_BIN                runtime-stub path (default: ./meshtastic_runtime_stub)
  NATIVE_BIN              native runtime path (default: ./firmware/.pio/build/native/meshtasticd)
  DAEMON_ARGS             Extra args passed to daemon
  RUNTIME_ARGS            Extra args passed to runtime
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stub)
            RUNTIME_MODE="stub"
            ;;
        --native)
            RUNTIME_MODE="native"
            ;;
        --dev)
            DEV_MODE=1
            RUNTIME_MODE="native"
            SKIP_DAEMON=1
            ;;
        --skip-daemon)
            SKIP_DAEMON=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
    shift
done

if [[ "$RUNTIME_MODE" == "stub" ]]; then
    RUNTIME_BIN="$STUB_BIN"
else
    RUNTIME_BIN="$NATIVE_BIN"
fi

if [[ ! -x "$RUNTIME_BIN" ]]; then
    echo "Runtime binary not found or not executable: $RUNTIME_BIN" >&2
    exit 1
fi

if [[ "$SKIP_DAEMON" -eq 0 && ! -x "$DAEMON_BIN" ]]; then
    echo "Daemon binary not found or not executable: $DAEMON_BIN" >&2
    exit 1
fi

rm -f "$SOCKET_PATH"

runtime_pid=""
daemon_pid=""

cleanup() {
    set +e
    if [[ -n "$daemon_pid" ]]; then
        kill "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
    if [[ -n "$runtime_pid" ]]; then
        kill "$runtime_pid" 2>/dev/null || true
        wait "$runtime_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

echo "[ipc] socket: $SOCKET_PATH"
echo "[ipc] runtime mode: $RUNTIME_MODE"
if [[ "$DEV_MODE" -eq 1 ]]; then
    echo "[ipc] dev mode: enabled (non-concentrator safe: daemon disabled)"
fi

MESHTASTIC_IPC_SOCKET="$SOCKET_PATH" "$RUNTIME_BIN" ${RUNTIME_ARGS:-} &
runtime_pid=$!
echo "[ipc] runtime pid: $runtime_pid"

for ((i = 0; i < SOCKET_WAIT_SECONDS * 10; i++)); do
    if [[ -S "$SOCKET_PATH" ]]; then
        break
    fi
    if ! kill -0 "$runtime_pid" 2>/dev/null; then
        echo "Runtime exited before creating socket" >&2
        exit 1
    fi
    # Busy wait with low overhead without shell sleep dependency.
    read -r -t 0.1 _ || true
done

if [[ ! -S "$SOCKET_PATH" ]]; then
    echo "Runtime socket not ready: $SOCKET_PATH" >&2
    if [[ "$RUNTIME_MODE" == "native" ]]; then
        echo "Hint: ensure native firmware was rebuilt with SX1302 IPC shim enabled and check runtime stderr logs." >&2
        echo "Use --stub for fallback IPC bridge tests." >&2
    fi
    exit 1
fi

echo "[ipc] runtime socket ready"

if [[ "$SKIP_DAEMON" -eq 1 ]]; then
    echo "[ipc] daemon launch skipped"
    wait "$runtime_pid"
    exit $?
fi

MESHTASTIC_IPC_SOCKET="$SOCKET_PATH" \
MESHTASTIC_IPC_REQUIRED="${MESHTASTIC_IPC_REQUIRED:-0}" \
"$DAEMON_BIN" ${DAEMON_ARGS:-} &
daemon_pid=$!
echo "[ipc] daemon pid: $daemon_pid"

wait "$daemon_pid"