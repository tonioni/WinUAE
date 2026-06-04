#!/usr/bin/env bash
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae_unix"}
LOG_DIR=${WINUAE_SMOKE_LOG_DIR:-/tmp}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-5}

A1200_ROM=${WINUAE_A1200_KICKSTART_ROM:-${WINUAE_KICKSTART_ROM:-}}
A4000_ROM=${WINUAE_A4000_KICKSTART_ROM:-}

if [ -z "$A1200_ROM" ] || [ -z "$A4000_ROM" ]; then
    echo "Set WINUAE_A1200_KICKSTART_ROM and WINUAE_A4000_KICKSTART_ROM before running this smoke test." >&2
    echo "WINUAE_KICKSTART_ROM can be used instead of WINUAE_A1200_KICKSTART_ROM." >&2
    exit 2
fi

if [ ! -f "$A1200_ROM" ]; then
    echo "A1200 Kickstart ROM not found: $A1200_ROM" >&2
    exit 2
fi

if [ ! -f "$A4000_ROM" ]; then
    echo "A4000 Kickstart ROM not found: $A4000_ROM" >&2
    exit 2
fi

if [ ! -x "$EXE" ]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD_DIR" --target winuae_unix
fi

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER SDL_VIDEODRIVER

base_a1200=(
    -s chipset=aga
    -s chipset_compatible=A1200
    -s cpu_model=68020
    -s chipmem_size=4
    -s cachesize=0
    -s sound_output=none
)

base_a4000=(
    -s chipset=aga
    -s chipset_compatible=A4000
    -s cpu_model=68030
    -s cpu_24bit_addressing=false
    -s chipmem_size=2
    -s cachesize=0
    -s sound_output=none
)

last_log=

run_case()
{
    local name=$1
    local rom=$2
    shift 2

    last_log="$LOG_DIR/winuae_unix_sound_${name}.log"
    : > "$last_log"

    "$EXE" \
        -s use_gui=no \
        -s kickstart_rom_file="$rom" \
        "$@" \
        > "$last_log" 2>&1 &

    local pid=$!
    sleep "$RUN_SECONDS"
    if kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || true
    fi
    wait "$pid" || true

    if grep -q "failed to load config" "$last_log" || grep -q "cfgfile_load_2 failed" "$last_log"; then
        echo "Unexpected config load failure in smoke log: $last_log" >&2
        exit 1
    fi
    grep -q "hardreset, memory cleared" "$last_log"
}

require_log()
{
    local pattern=$1
    if ! grep -Eq "$pattern" "$last_log"; then
        echo "Missing expected log pattern '$pattern' in $last_log" >&2
        exit 1
    fi
}

run_pci_case()
{
    local name=$1
    local bridge_pattern=$2
    local board_pattern=$3
    shift 3

    run_case "$name" "$A4000_ROM" "${base_a4000[@]}" "$@"
    require_log "$bridge_pattern"
    require_log "$board_pattern"
}

run_case toccata "$A1200_ROM" "${base_a1200[@]}" -s toccata_rom_file=:ENABLED
require_log "Card [0-9]+: 'Toccata'"
require_log "Toccata"

run_case prelude "$A1200_ROM" "${base_a1200[@]}" -s prelude_rom_file=:ENABLED
require_log "Card [0-9]+: 'Prelude'"
require_log "mapped_malloc .* Prelude"

run_case prelude1200 "$A1200_ROM" "${base_a1200[@]}" -s prelude1200_rom_file=:ENABLED
require_log "Prelude1200"

run_case uaesnd_z2 "$A1200_ROM" "${base_a1200[@]}" -s uaesnd_z2_rom_file=:ENABLED
require_log "Card [0-9]+: 'UAESND Z2'"
require_log "Card [0-9]+: Z2 .*uaesnd z2"
require_log "UAESND memory"

run_case uaesnd_z3 "$A4000_ROM" "${base_a4000[@]}" -s uaesnd_z3_rom_file=:ENABLED
require_log "Card [0-9]+: 'UAESND Z3'"
require_log "Card [0-9]+: Z3 .*uaesnd z3"
require_log "UAESND memory"

run_case uaeboard_z2 "$A1200_ROM" "${base_a1200[@]}" -s uaeboard_z2_rom_file=:ENABLED
require_log "Card [0-9]+: 'UAEBOARD Z2'"
require_log "Card [0-9]+: Z2 .*uaesnd z2"
require_log "UAESND memory"

run_case uaeboard_z3 "$A4000_ROM" "${base_a4000[@]}" -s uaeboard_z3_rom_file=:ENABLED
require_log "Card [0-9]+: 'UAEBOARD Z3'"
require_log "Card [0-9]+: Z3 .*uaesnd z3"
require_log "UAESND memory"

run_pci_case es1370_prometheus "PCI bridge 'Prometheus'" "ES1370" \
    -s prometheus_rom_file=:ENABLED \
    -s es1370_rom_file=:ENABLED

run_pci_case es1370_prometheusfirestorm \
    "PCI bridge 'Prometheus FireStorm'" "ES1370" \
    -s prometheusfirestorm_rom_file=:ENABLED \
    -s es1370_rom_file=:ENABLED

run_pci_case es1370_mediator4000 "PCI bridge 'Mediator 4000'" "ES1370" \
    -s mediator_rom_file=:ENABLED \
    -s mediator_rom_options=subtype=4000mkii \
    -s es1370_rom_file=:ENABLED

run_pci_case es1370_grex "PCI bridge 'G-REX'" "ES1370" \
    -s grex_rom_file=:ENABLED \
    -s es1370_rom_file=:ENABLED

run_pci_case fm801_prometheus "PCI bridge 'Prometheus'" "FM801" \
    -s prometheus_rom_file=:ENABLED \
    -s fm801_rom_file=:ENABLED

run_pci_case fm801_prometheusfirestorm \
    "PCI bridge 'Prometheus FireStorm'" "FM801" \
    -s prometheusfirestorm_rom_file=:ENABLED \
    -s fm801_rom_file=:ENABLED

run_pci_case fm801_mediator4000 "PCI bridge 'Mediator 4000'" "FM801" \
    -s mediator_rom_file=:ENABLED \
    -s mediator_rom_options=subtype=4000mkii \
    -s fm801_rom_file=:ENABLED

run_pci_case fm801_grex "PCI bridge 'G-REX'" "FM801" \
    -s grex_rom_file=:ENABLED \
    -s fm801_rom_file=:ENABLED

echo "Unix sound-board smoke test passed. Logs: $LOG_DIR/winuae_unix_sound_*.log"
