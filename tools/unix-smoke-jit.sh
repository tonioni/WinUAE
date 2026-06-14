#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_smoke_jit.log}
CACHE_SIZE=${WINUAE_JIT_CACHE_SIZE:-8192}

export WINUAE_SMOKE_LOG="$LOG"
export WINUAE_SMOKE_SKIP_AUDIO_CHECK=1

"$ROOT_DIR/tools/unix-smoke-a1200.sh" \
    -s sound_output=none \
    -s cycle_exact=false \
    -s cachesize="$CACHE_SIZE" \
    -s cpu_speed=max \
    "$@"

grep -q "JIT: cache=$CACHE_SIZE" "$LOG"
grep -q "translation cache" "$LOG"
if grep -q "JIT=0" "$LOG"; then
    echo "JIT did not stay enabled. Log: $LOG" >&2
    exit 1
fi

echo "Unix JIT smoke test passed. Log: $LOG"
