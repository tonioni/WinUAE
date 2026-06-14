#!/usr/bin/env sh
set -eu

WINUAE_SMOKE_RTG_Z3=1
WINUAE_SMOKE_UAEGFX=1
: "${WINUAE_SMOKE_SECONDS:=25}"
export WINUAE_SMOKE_RTG_Z3 WINUAE_SMOKE_UAEGFX WINUAE_SMOKE_SECONDS

exec "$(dirname -- "$0")/unix-smoke-a1200.sh" "$@"
