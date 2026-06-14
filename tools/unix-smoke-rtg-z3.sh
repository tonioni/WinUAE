#!/usr/bin/env sh
set -eu

WINUAE_SMOKE_RTG_Z3=1
export WINUAE_SMOKE_RTG_Z3

exec "$(dirname -- "$0")/unix-smoke-a1200.sh"
