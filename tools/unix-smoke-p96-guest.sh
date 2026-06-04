#!/usr/bin/env bash
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${WINUAE_BUILD_DIR:-/tmp/winuae_cmake_build}
EXE=${WINUAE_EXE:-"$BUILD_DIR/winuae_unix"}
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_p96_guest_smoke.log}
RUN_SECONDS=${WINUAE_SMOKE_SECONDS:-45}
Z3=${WINUAE_P96_Z3:-0}
USER_ARGS=("$@")

if [ "$Z3" = "1" ]; then
    ROM=${WINUAE_A4000_KICKSTART_ROM:-${WINUAE_KICKSTART_ROM:-}}
else
    ROM=${WINUAE_KICKSTART_ROM:-}
fi
WORKBENCH=${WINUAE_P96_WORKBENCH_DIR:-}
SCREENMODE=${WINUAE_P96_SCREENMODE:-}
OPEN_SCREEN=${WINUAE_P96_OPEN_SCREEN:-}
OPEN_SCREEN_BINARY=${WINUAE_P96_OPEN_SCREEN_BINARY:-}
OPEN_SCREEN_CC=${WINUAE_M68K_CC:-m68k-amigaos-gcc}
EXPECT_DRAW_BLITS=${WINUAE_P96_EXPECT_DRAW_BLITS:-}
EXPECT_MODE_MASK=${WINUAE_P96_EXPECT_MODE_MASK:-}
EXPECT_RESINFO_BYTES=${WINUAE_P96_EXPECT_RESINFO_BYTES:-}
EXPECT_HARDWARE_SPRITE=${WINUAE_P96_EXPECT_HARDWARE_SPRITE:-}
EXPECT_HARDWARE_VBLANK=${WINUAE_P96_EXPECT_HARDWARE_VBLANK:-}
KEEP_WORKDIR=${WINUAE_KEEP_SMOKE_WORKDIR:-0}

if [ -z "$ROM" ] || [ -z "$WORKBENCH" ]; then
    echo "Set WINUAE_KICKSTART_ROM and WINUAE_P96_WORKBENCH_DIR before running this smoke test." >&2
    exit 2
fi

if [ ! -f "$ROM" ]; then
    echo "Kickstart ROM not found: $ROM" >&2
    exit 2
fi

if [ ! -d "$WORKBENCH" ]; then
    echo "P96 Workbench directory not found: $WORKBENCH" >&2
    exit 2
fi

if [ ! -f "$WORKBENCH/Devs/Monitors/uaegfx" ] || [ ! -f "$WORKBENCH/Libs/Picasso96API.library" ]; then
    echo "P96 Workbench directory must contain Devs/Monitors/uaegfx and Libs/Picasso96API.library: $WORKBENCH" >&2
    exit 2
fi

if [ ! -x "$EXE" ]; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$BUILD_DIR" --target winuae_unix
fi

WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/winuae_p96_guest.XXXXXX")
pid=
cleanup() {
    if [ "$KEEP_WORKDIR" = "1" ]; then
        echo "Keeping P96 guest smoke workdir: $WORKDIR" >&2
    else
        rm -rf "$WORKDIR"
    fi
}
trap 'kill -INT "$pid" 2>/dev/null || true; cleanup' INT TERM EXIT

cp -R "$WORKBENCH" "$WORKDIR/Workbench"

write_screenmode_prefs() {
    mode=$1
    file=$2
    case "$mode" in
        640x480x8)
            display_id='\120\003\020\000'
            depth='\000\010'
            ;;
        800x600x8)
            display_id='\120\004\020\000'
            depth='\000\010'
            ;;
        1024x768x8)
            display_id='\120\005\020\000'
            depth='\000\010'
            ;;
        640x480x15)
            display_id='\120\003\020\000'
            depth='\000\017'
            ;;
        800x600x15)
            display_id='\120\004\020\000'
            depth='\000\017'
            ;;
        1024x768x15)
            display_id='\120\005\020\000'
            depth='\000\017'
            ;;
        640x480x16)
            display_id='\120\003\020\000'
            depth='\000\020'
            ;;
        800x600x16)
            display_id='\120\004\020\000'
            depth='\000\020'
            ;;
        1024x768x16)
            display_id='\120\005\020\000'
            depth='\000\020'
            ;;
        *)
            echo "Unsupported WINUAE_P96_SCREENMODE: $mode" >&2
            exit 2
            ;;
    esac

    mkdir -p "$(dirname "$file")"
    {
        printf 'FORM\000\000\000\066PREFPRHD\000\000\000\006\000\000\000\000\000\000'
        printf 'SCRM\000\000\000\034'
        printf '\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'
        printf "$display_id"
        printf '\377\377\377\377'
        printf "$depth"
        printf '\000\001'
    } > "$file"
}

if [ -n "$SCREENMODE" ]; then
    write_screenmode_prefs "$SCREENMODE" "$WORKDIR/Workbench/Prefs/Env-Archive/Sys/ScreenMode.prefs"
    mkdir -p "$WORKDIR/Workbench/Prefs/Env-Archive/Picasso96"
    printf 'All' > "$WORKDIR/Workbench/Prefs/Env-Archive/Picasso96/ShowModes"
fi

open_screen_args=
if [ -n "$OPEN_SCREEN" ]; then
    case "$OPEN_SCREEN" in
        640x480x8)
            open_screen_args='0x50031000 640 480 8'
            ;;
        800x600x8)
            open_screen_args='0x50041000 800 600 8'
            ;;
        1024x768x8)
            open_screen_args='0x50051000 1024 768 8'
            ;;
        640x480x15)
            open_screen_args='0x50031000 640 480 15'
            ;;
        800x600x15)
            open_screen_args='0x50041000 800 600 15'
            ;;
        1024x768x15)
            open_screen_args='0x50051000 1024 768 15'
            ;;
        640x480x16)
            open_screen_args='0x50031000 640 480 16'
            ;;
        800x600x16)
            open_screen_args='0x50041000 800 600 16'
            ;;
        1024x768x16)
            open_screen_args='0x50051000 1024 768 16'
            ;;
        640x480x24)
            open_screen_args='0x50031000 640 480 24'
            ;;
        800x600x24)
            open_screen_args='0x50041000 800 600 24'
            ;;
        1024x768x24)
            open_screen_args='0x50051000 1024 768 24'
            ;;
        640x480x32)
            open_screen_args='0x50031000 640 480 32'
            ;;
        800x600x32)
            open_screen_args='0x50041000 800 600 32'
            ;;
        1024x768x32)
            open_screen_args='0x50051000 1024 768 32'
            ;;
        *)
            echo "Unsupported WINUAE_P96_OPEN_SCREEN: $OPEN_SCREEN" >&2
            exit 2
            ;;
    esac
    if [ -n "$OPEN_SCREEN_BINARY" ]; then
        cp "$OPEN_SCREEN_BINARY" "$WORKDIR/Workbench/p96-open-screen"
    else
        if ! command -v "$OPEN_SCREEN_CC" >/dev/null 2>&1; then
            echo "P96 open-screen smoke requires $OPEN_SCREEN_CC or WINUAE_P96_OPEN_SCREEN_BINARY." >&2
            exit 2
        fi
        "$OPEN_SCREEN_CC" -noixemul -m68020 -Os \
            "$ROOT_DIR/tools/amiga/p96-open-screen.c" \
            -o "$WORKDIR/Workbench/p96-open-screen"
    fi
fi

{
cat <<'EOF'
C:MakeDir RAM:T RAM:ENV RAM:ENV/Sys RAM:Clipboards
C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ
Assign ENV: RAM:ENV
Assign T: RAM:T
Assign CLIPS: RAM:Clipboards
Assign LIBS: SYS:Classes ADD
Echo "before uaegfx smoke" >DH0:unix-p96-before-uaegfx-smoke
DEVS:Monitors/uaegfx
C:IPrefs
EOF
if [ -n "$open_screen_args" ]; then
    printf 'DH0:p96-open-screen %s >DH0:unix-p96-open-screen-smoke\n' "$open_screen_args"
fi
cat <<'EOF'
C:LoadWB
Echo "after uaegfx smoke" >DH0:unix-p96-after-uaegfx-smoke
Wait 20
EndCLI >NIL:
EOF
} > "$WORKDIR/Workbench/S/Startup-Sequence"

: > "$LOG"

SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy}
export SDL_AUDIODRIVER SDL_VIDEODRIVER

set -- \
    -s use_gui=no \
    -s kickstart_rom_file="$ROM" \
    -s filesystem2="rw,DH0:System:$WORKDIR/Workbench,10" \
    -s nr_floppies=0 \
    -s chipset=aga \
    -s cachesize=0

if [ "$Z3" = "1" ]; then
    set -- "$@" \
        -s chipset_compatible=A4000 \
        -s cpu_model=68040 \
        -s fpu_model=68040 \
        -s cpu_24bit_addressing=false \
        -s chipmem_size=4 \
        -s a3000mem_size=8 \
        -s gfxcard_size=16 \
        -s gfxcard_type=ZorroIII
else
    set -- "$@" \
        -s chipset_compatible=A1200 \
        -s cpu_model=68020 \
        -s chipmem_size=2 \
        -s fastmem_size=4 \
        -s gfxcard_size=4 \
        -s gfxcard_type=ZorroII
fi

"$EXE" "$@" "${USER_ARGS[@]}" > "$LOG" 2>&1 &

pid=$!

sleep "$RUN_SECONDS"
if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
fi
wait "$pid" || true
pid=
trap cleanup EXIT

test -f "$WORKDIR/Workbench/unix-p96-before-uaegfx-smoke"
test -f "$WORKDIR/Workbench/unix-p96-after-uaegfx-smoke"
grep -q "Known ROM" "$LOG"
grep -q "FS: mounted virtual unit DH0" "$LOG"
grep -q "Unix uaegfx.card" "$LOG"
grep -q "Unix RTG P96 RESINFO" "$LOG"
grep -q "Unix RTG FindCard:" "$LOG"
grep -q "Unix RTG InitCard:" "$LOG"
grep -q "Unix RTG SetGC:" "$LOG"
grep -q "Unix RTG SetPanning:" "$LOG"
grep -q "Unix RTG SetSwitch:" "$LOG"
if [ -n "$EXPECT_HARDWARE_SPRITE" ]; then
    grep -q "Unix RTG hardware sprite support enabled" "$LOG"
fi
if [ -n "$EXPECT_HARDWARE_VBLANK" ]; then
    grep -q "Unix RTG vblank interrupt support enabled" "$LOG"
fi
if [ -n "$EXPECT_MODE_MASK" ]; then
    grep -qi "Unix RTG mode mask: $EXPECT_MODE_MASK" "$LOG"
fi
if [ -n "$EXPECT_RESINFO_BYTES" ]; then
    grep -Eq "Unix RTG P96 RESINFO: .*\($EXPECT_RESINFO_BYTES bytes\)" "$LOG"
fi
if [ -n "$SCREENMODE" ]; then
    grep -q "Unix RTG SetGC: $SCREENMODE" "$LOG"
fi
if [ -n "$OPEN_SCREEN" ]; then
    grep -q "OPENSCREEN OK" "$WORKDIR/Workbench/unix-p96-open-screen-smoke"
    grep -q "Unix RTG SetGC: $OPEN_SCREEN" "$LOG"
fi
if [ -n "$EXPECT_DRAW_BLITS" ]; then
    grep -q "DRAW OK" "$WORKDIR/Workbench/unix-p96-open-screen-smoke"
    grep -q "Unix RTG FillRect" "$LOG"
    grep -q "Unix RTG BlitTemplate" "$LOG"
    grep -q "Unix RTG BlitPattern" "$LOG"
    case "$EXPECT_DRAW_BLITS" in
        chunky)
            grep -q "Unix RTG BlitPlanar2Chunky" "$LOG"
            ;;
        direct)
            grep -q "Unix RTG BlitPlanar2Direct" "$LOG"
            ;;
        *)
            echo "Unsupported WINUAE_P96_EXPECT_DRAW_BLITS: $EXPECT_DRAW_BLITS" >&2
            exit 2
            ;;
    esac
fi
if grep -q "not executable" "$LOG" || grep -q "failed to load config" "$LOG" || grep -q "cfgfile_load_2 failed" "$LOG"; then
    echo "Unexpected failure in P96 guest smoke log: $LOG" >&2
    exit 1
fi

echo "Unix P96 guest smoke test passed. Log: $LOG"
