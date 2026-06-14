#!/usr/bin/env sh
set -eu

WINUAE_SMOKE_SANA2=1
export WINUAE_SMOKE_SANA2

exec "$(dirname -- "$0")/unix-smoke-a1200.sh"
