#!/usr/bin/env sh
set -eu

WINUAE_SMOKE_A2065=1
export WINUAE_SMOKE_A2065

exec "$(dirname -- "$0")/unix-smoke-a1200.sh"
