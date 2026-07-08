#!/usr/bin/env sh
set -eu

# Verifies that configuration values survive the trip through the Qt
# launcher's widgets: load a config, export the merged config, and compare
# the keys that GitHub issue #1 reported as unreliable (CPU, FPU, memory).

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae"}
WORK=$(mktemp -d /tmp/winuae_config_roundtrip.XXXXXX)
trap 'rm -rf "$WORK"' EXIT INT TERM

if [ ! -x "$EXE" ]; then
    echo "error: emulator executable not found: $EXE" >&2
    exit 1
fi

config_value() {
    key=$1
    file=$2
    sed -n "s/^${key}=//p" "$file" | head -n 1
}

normalize_bool() {
    case "$1" in
        true|yes|1) echo "true" ;;
        false|no|0|"") echo "false" ;;
        *) echo "$1" ;;
    esac
}

run_case() {
    name=$1
    input=$2
    keys=$3
    absent_keys=${4:-}
    output="$WORK/$name-out.uae"

    env QT_QPA_PLATFORM=offscreen \
        WINUAE_INI="$WORK/$name.ini" \
        WINUAE_QT_CONFIG_ROUNDTRIP_OUT="$output" \
        "$EXE" -f "$input" >/dev/null 2>&1 || {
            echo "error: $name: emulator exited with failure" >&2
            exit 1
        }
    if [ ! -f "$output" ]; then
        echo "error: $name: no merged config was exported" >&2
        exit 1
    fi

    failed=0
    for key in $keys; do
        in_value=$(config_value "$key" "$input")
        out_value=$(config_value "$key" "$output")
        case "$in_value" in
            true|false|yes|no)
                in_value=$(normalize_bool "$in_value")
                out_value=$(normalize_bool "$out_value")
                ;;
        esac
        if [ "$in_value" != "$out_value" ]; then
            echo "FAIL: $name: $key: '$in_value' became '$out_value'" >&2
            failed=1
        fi
    done
    for key in $absent_keys; do
        if grep -q "^${key}=" "$output"; then
            echo "FAIL: $name: $key was added during roundtrip" >&2
            failed=1
        fi
    done
    if [ "$failed" -ne 0 ]; then
        echo "--- merged config ---" >&2
        cat "$output" >&2
        exit 1
    fi
    echo "ok: $name"
}

cat > "$WORK/a4000.uae" <<EOF
config_description=Roundtrip A4000
cpu_model=68040
fpu_model=68040
cpu_24bit_addressing=false
cpu_compatible=false
cachesize=0
chipset=aga
chipset_compatible=A4000
chipmem_size=4
fastmem_size=8
z3mem_size=64
megachipmem_size=32
mbresmem_size=16
floppy0=$WORK/boot.adf
nr_floppies=2
unix.gfx_shader=scale2x
EOF

run_case a4000 "$WORK/a4000.uae" \
    "cpu_model fpu_model cpu_24bit_addressing cachesize chipset chipmem_size fastmem_size z3mem_size megachipmem_size mbresmem_size floppy0 nr_floppies unix.gfx_shader"

cat > "$WORK/a500.uae" <<EOF
config_description=Roundtrip A500
cpu_model=68000
cpu_24bit_addressing=true
cpu_compatible=true
chipset=ocs
chipmem_size=1
bogomem_size=2
fastmem_size=2
cpu_memory_cycle_exact=true
EOF

run_case a500 "$WORK/a500.uae" \
    "cpu_model cpu_24bit_addressing cpu_compatible chipset chipmem_size bogomem_size fastmem_size cpu_memory_cycle_exact"

cat > "$WORK/a3000.uae" <<EOF
config_description=Roundtrip A3000
cpu_model=68030
fpu_model=68882
mmu_model=68030
cpu_24bit_addressing=false
chipset=ecs
chipmem_size=2
bogomem_size=4
z3mem_size=128
EOF

run_case a3000 "$WORK/a3000.uae" \
    "cpu_model fpu_model mmu_model cpu_24bit_addressing chipset chipmem_size bogomem_size z3mem_size"

cat > "$WORK/a4091.uae" <<EOF
config_description=Roundtrip A4091
quickstart=A4000,1
chipset_compatible=A4000
a4091_rom_file=rea4091.rom
hardfile2=rw,:/tmp/disk.hdf,0,0,0,512,0,,scsi0_a4091
uaehf1=cd2,ro,:,0,0,0,2048,0,,scsi2_a4091
cdimage2=/tmp/cd.iso
EOF

run_case a4091 "$WORK/a4091.uae" \
    "a4091_rom_file hardfile2 uaehf1 cdimage2" \
    "uaescsimode unix.uaescsimode"

echo "winuae config roundtrip smoke passed"
