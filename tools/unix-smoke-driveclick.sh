#!/usr/bin/env sh
set -eu

WINUAE_SMOKE_DRIVECLICK=1
export WINUAE_SMOKE_DRIVECLICK

exec "$(dirname -- "$0")/unix-smoke-a1200.sh"
