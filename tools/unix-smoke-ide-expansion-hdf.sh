#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae_unix"}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-8}

BOARD=${WINUAE_IDE_EXPANSION_BOARD:-alfapower}
ROM=${WINUAE_A1200_KICKSTART_ROM:-${WINUAE_KICKSTART_ROM:-}}
HDF=${WINUAE_IDE_EXPANSION_HDF:-${WINUAE_HARDFILE0:-}}
HDF_CREATED=0

case "$BOARD" in
    alfapower)
        ROM_KEY=alfapower_rom_file
        CONTROLLER=ide0_alfapower
        EXPECTED_CARD="AlfaPower/AT-Bus 2008"
        ;;
    ripple)
        ROM_KEY=ripple_rom_file
        CONTROLLER=ide0_ripple,ATA2+
        EXPECTED_CARD="RIPPLE"
        ;;
    *)
        echo "Unsupported WINUAE_IDE_EXPANSION_BOARD: $BOARD" >&2
        echo "Supported values: alfapower, ripple" >&2
        exit 2
        ;;
esac

LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_${BOARD}_hdf_smoke.log}

if [ -z "$ROM" ]; then
    echo "Set WINUAE_A1200_KICKSTART_ROM or WINUAE_KICKSTART_ROM before running this smoke test." >&2
    exit 2
fi

if [ ! -f "$ROM" ]; then
    echo "A1200 Kickstart ROM not found: $ROM" >&2
    exit 2
fi

if [ -z "$HDF" ]; then
    HDF=$(mktemp "${TMPDIR:-/tmp}/winuae_${BOARD}_hdf.XXXXXX")
    HDF_CREATED=1
    dd if=/dev/zero of="$HDF" bs=1048576 count="${WINUAE_IDE_EXPANSION_HDF_MB:-8}" >/dev/null 2>&1
elif [ ! -f "$HDF" ]; then
    echo "IDE expansion HDF image not found: $HDF" >&2
    exit 2
fi

cleanup() {
    if [ "$HDF_CREATED" = 1 ]; then
        rm -f "$HDF"
    fi
}

if [ ! -x "$EXE" ]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD_DIR" --target winuae_unix
fi

: > "$LOG"

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER SDL_VIDEODRIVER

"$EXE" \
    -s use_gui=no \
    -s kickstart_rom_file="$ROM" \
    -s "$ROM_KEY=:ENABLED" \
    -s hardfile2="rw,DH0:$HDF,0,0,0,512,0,,$CONTROLLER" \
    -s chipset=aga \
    -s chipset_compatible=A1200 \
    -s cpu_model=68020 \
    -s cpu_24bit_addressing=true \
    -s cachesize=0 \
    > "$LOG" 2>&1 &

pid=$!
trap 'kill -INT "$pid" 2>/dev/null || true; cleanup' INT TERM EXIT

sleep "$RUN_SECONDS"
if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
fi
wait "$pid" || true
trap - INT TERM EXIT
cleanup

grep -q "Known ROM" "$LOG"
grep -q "Adding IDE HD '$BOARD' unit 0" "$LOG"
grep -q "Card 03: '$EXPECTED_CARD'" "$LOG"
grep -q "hardreset, memory cleared" "$LOG"
if grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected config load failure in smoke log: $LOG" >&2
    exit 1
fi

echo "Unix $EXPECTED_CARD HDF smoke test passed. Log: $LOG"
