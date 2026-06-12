#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae"}
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_path_config_smoke.log}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-5}
TMPBASE=${TMPDIR:-/tmp}
WORKDIR="$TMPBASE/winuae-path-smoke-$$"

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

cleanup()
{
    rm -f "$WORKDIR/path-smoke.uae" "$WORKDIR/roms/kick.rom" "$WORKDIR/disks/install.adf"
    rmdir "$WORKDIR/roms" "$WORKDIR/disks" "$WORKDIR" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$WORKDIR/roms" "$WORKDIR/disks"
ln -sf "$ROM" "$WORKDIR/roms/kick.rom"
ln -sf "$ADF" "$WORKDIR/disks/install.adf"

{
    printf '%s\n' 'use_gui=no'
    printf '%s\n' 'kickstart_rom_file=roms/kick.rom'
    printf '%s\n' 'nr_floppies=1'
    printf '%s\n' 'chipset=aga'
    printf '%s\n' 'chipset_compatible=A1200'
    printf '%s\n' 'cpu_model=68020'
    printf '%s\n' 'chipmem_size=4'
    printf '%s\n' 'cachesize=0'
} > "$WORKDIR/path-smoke.uae"

: > "$LOG"

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
WINUAE_PATH_SMOKE_ADF=disks/install.adf
export SDL_AUDIODRIVER SDL_VIDEODRIVER WINUAE_PATH_SMOKE_ADF

sh -c 'cd "$1" && exec "$2" -f path-smoke.uae -s "floppy0=\$WINUAE_PATH_SMOKE_ADF"' sh "$WORKDIR" "$EXE" > "$LOG" 2>&1 &

pid=$!
trap 'kill -INT "$pid" 2>/dev/null || true; cleanup' INT TERM EXIT

sleep "$RUN_SECONDS"
if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
fi
wait "$pid" || true
trap cleanup EXIT

grep -q "Known ROM" "$LOG"
grep -q "SDL3: audio initialized" "$LOG"
grep -q "hardreset, memory cleared" "$LOG"
if grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected config load failure in smoke log: $LOG" >&2
    exit 1
fi

echo "Unix path config smoke test passed. Log: $LOG"
