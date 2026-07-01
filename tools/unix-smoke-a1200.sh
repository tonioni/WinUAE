#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae"}
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_smoke.log}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-5}
A2065=${WINUAE_SMOKE_A2065:-0}
SANA2=${WINUAE_SMOKE_SANA2:-0}
RTG_Z3=${WINUAE_SMOKE_RTG_Z3:-0}
UAEGFX=${WINUAE_SMOKE_UAEGFX:-0}
UAEGFX_DRIVER=${WINUAE_SMOKE_UAEGFX_DRIVER:-0}
UAEGFX_SCREEN=${WINUAE_SMOKE_UAEGFX_SCREEN:-0}
DRIVECLICK=${WINUAE_SMOKE_DRIVECLICK:-0}
SKIP_AUDIO_CHECK=${WINUAE_SMOKE_SKIP_AUDIO_CHECK:-0}

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

if [ ! -x "$EXE" ]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD_DIR" --target winuae_unix
fi

: > "$LOG"

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER SDL_VIDEODRIVER

set -- "$@"
if [ "$A2065" = "1" ]; then
    set -- "$@" -s a2065=slirp
fi
if [ "$SANA2" = "1" ]; then
    set -- "$@" -s sana2=true
fi
if [ "$RTG_Z3" = "1" ]; then
    set -- "$@" \
        -s cpu_24bit_addressing=false \
        -s gfxcard_size=16 \
        -s gfxcard_type=ZorroIII
fi
if [ "$DRIVECLICK" = "1" ]; then
    set -- "$@" \
        -s floppy0sound=1 \
        -s floppy0soundvolume_empty=0 \
        -s floppy0soundvolume_disk=0
fi

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
    "$@" \
    > "$LOG" 2>&1 &

pid=$!
trap 'kill -INT "$pid" 2>/dev/null || true' INT TERM EXIT

sleep "$RUN_SECONDS"
if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
fi
wait "$pid" || true
trap - INT TERM EXIT

grep -q "Known ROM" "$LOG"
if [ "$SKIP_AUDIO_CHECK" != "1" ]; then
    grep -q "SDL3: audio initialized" "$LOG"
fi
grep -q "hardreset, memory cleared" "$LOG"
if [ "$A2065" = "1" ]; then
    grep -q "A2065" "$LOG"
fi
if [ "$SANA2" = "1" ]; then
    grep -q "uaenet.device reset" "$LOG"
    grep -q "SLIRP User Mode NAT" "$LOG"
fi
if [ "$RTG_Z3" = "1" ]; then
    grep -q "UAE RTG" "$LOG"
    grep -q "RTG RAM" "$LOG"
fi
if [ "$UAEGFX" = "1" ]; then
    grep -q "Unix uaegfx.card" "$LOG"
    grep -q "Unix RTG P96 RESINFO" "$LOG"
fi
if [ "$UAEGFX_DRIVER" = "1" ]; then
    grep -q "Unix RTG FindCard:" "$LOG"
    grep -q "Unix RTG InitCard:" "$LOG"
    grep -q "Unix RTG uaegfx.card code:" "$LOG"
fi
if [ "$UAEGFX_SCREEN" = "1" ]; then
    grep -q "Unix RTG SetGC:" "$LOG"
    grep -q "Unix RTG SetPanning:" "$LOG"
    grep -q "Unix RTG SetSwitch:" "$LOG"
fi
if [ "$DRIVECLICK" = "1" ]; then
    grep -q "Unix driveclick: loaded built-in A500 sample set" "$LOG"
fi
if grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected config load failure in smoke log: $LOG" >&2
    exit 1
fi

echo "Unix smoke test passed. Log: $LOG"
