#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae"}
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_a4091_hdf_smoke.log}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-8}

ROM=${WINUAE_A4000_KICKSTART_ROM:-${WINUAE_KICKSTART_ROM:-}}
A4091_ROM=${WINUAE_A4091_ROM:-}
HDF=${WINUAE_A4091_HDF:-${WINUAE_HARDFILE0:-}}

if [ -z "$ROM" ] || [ -z "$A4091_ROM" ] || [ -z "$HDF" ]; then
    echo "Set WINUAE_A4000_KICKSTART_ROM, WINUAE_A4091_ROM, and WINUAE_A4091_HDF before running this smoke test." >&2
    echo "WINUAE_KICKSTART_ROM can be used instead of WINUAE_A4000_KICKSTART_ROM, and WINUAE_HARDFILE0 can be used instead of WINUAE_A4091_HDF." >&2
    exit 2
fi

if [ ! -f "$ROM" ]; then
    echo "A4000 Kickstart ROM not found: $ROM" >&2
    exit 2
fi

if [ ! -f "$A4091_ROM" ]; then
    echo "A4091 ROM not found: $A4091_ROM" >&2
    exit 2
fi

if [ ! -f "$HDF" ]; then
    echo "A4091 HDF image not found: $HDF" >&2
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

"$EXE" \
    -s use_gui=no \
    -s kickstart_rom_file="$ROM" \
    -s a4091_rom_file="$A4091_ROM" \
    -s hardfile2="rw,DH0:$HDF,0,0,0,512,0,,scsi0_a4091" \
    -s chipset=aga \
    -s chipset_compatible=A4000 \
    -s cpu_model=68030 \
    -s cpu_24bit_addressing=false \
    -s cachesize=0 \
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
grep -q "Adding SCSI HD 'a4091' unit 0" "$LOG"
grep -q "Card 02: 'A4091'" "$LOG"
grep -q "NCR53C700/800" "$LOG"
grep -q "hardreset, memory cleared" "$LOG"
if grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected config load failure in smoke log: $LOG" >&2
    exit 1
fi

echo "Unix A4091 HDF smoke test passed. Log: $LOG"
