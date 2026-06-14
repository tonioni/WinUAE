#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae_unix"}
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_serial_tcp_smoke.log}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-5}
CONNECT_TIMEOUT=${WINUAE_SERIAL_TCP_TIMEOUT:-20}
CONNECT_HOST=${WINUAE_SERIAL_TCP_CONNECT_HOST:-127.0.0.1}
LISTEN_HOST=${WINUAE_SERIAL_TCP_LISTEN_HOST:-$CONNECT_HOST}
PORT=${WINUAE_SERIAL_TCP_PORT:-51234}
WAIT_MODE=${WINUAE_SERIAL_TCP_WAIT:-1}
NC=${WINUAE_NC:-nc}

ROM=${WINUAE_KICKSTART_ROM:-}
ADF=${WINUAE_FLOPPY0:-}

if [ -z "$ROM" ] || [ -z "$ADF" ]; then
    echo "Set WINUAE_KICKSTART_ROM and WINUAE_FLOPPY0 before running this smoke test." >&2
    exit 2
fi

if [ ! -f "$ROM" ]; then
    echo "Kickstart ROM not found: $ROM" >&2
    exit 2
fi

if [ ! -f "$ADF" ]; then
    echo "ADF image not found: $ADF" >&2
    exit 2
fi

if ! command -v "$NC" >/dev/null 2>&1; then
    echo "Netcat command not found: $NC" >&2
    exit 2
fi

if [ ! -x "$EXE" ]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD_DIR" --target winuae_unix
fi

SERIAL_SPEC="TCP://$LISTEN_HOST:$PORT"
if [ "$WAIT_MODE" = "1" ]; then
    SERIAL_SPEC="$SERIAL_SPEC/wait"
fi

: > "$LOG"

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER SDL_VIDEODRIVER

"$EXE" \
    -s use_gui=no \
    -s kickstart_rom_file="$ROM" \
    -s floppy0="$ADF" \
    -s nr_floppies=1 \
    -s chipset=aga \
    -s chipset_compatible=A1200 \
    -s cpu_model=68020 \
    -s chipmem_size=4 \
    -s cachesize=0 \
    -s unix.serial_port="$SERIAL_SPEC" \
    -s serial_on_demand=false \
    > "$LOG" 2>&1 &

pid=$!
trap 'kill -INT "$pid" 2>/dev/null || true' INT TERM EXIT

i=0
while ! grep -q "SERIAL_TCP: listening on" "$LOG"; do
    if grep -q "SERIAL_TCP: failed to listen" "$LOG" || grep -q "SERIAL: Could not open device" "$LOG"; then
        echo "TCP serial listener failed to start. Log: $LOG" >&2
        exit 1
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "Emulator exited before TCP serial listener started. Log: $LOG" >&2
        exit 1
    fi
    if [ "$i" -ge "$CONNECT_TIMEOUT" ]; then
        echo "Timed out waiting for TCP serial listener. Log: $LOG" >&2
        exit 1
    fi
    i=$((i + 1))
    sleep 1
done

(
    printf 'serial-smoke\r\n'
    sleep 1
) | "$NC" "$CONNECT_HOST" "$PORT" >/dev/null 2>&1 &
nc_pid=$!
sleep 1
kill "$nc_pid" 2>/dev/null || true
wait "$nc_pid" 2>/dev/null || true

sleep "$RUN_SECONDS"
if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
fi
wait "$pid" || true
trap - INT TERM EXIT

grep -q "SERIAL_TCP: listening on" "$LOG"
grep -q "SERIAL_TCP: connection accepted" "$LOG"
grep -q "Known ROM" "$LOG"
grep -q "SDL3: audio initialized" "$LOG"
grep -q "hardreset, memory cleared" "$LOG"
if grep -q "SERIAL_TCP: failed to listen" "$LOG" || grep -q "SERIAL: Could not open device" "$LOG"; then
    echo "TCP serial listener failed during smoke test. Log: $LOG" >&2
    exit 1
fi
if grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected config load failure in smoke log: $LOG" >&2
    exit 1
fi

echo "Unix TCP serial smoke test passed. Log: $LOG"
