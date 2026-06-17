#!/usr/bin/env sh
set -eu

WINUAE_P96_Z3=1
: "${WINUAE_SMOKE_LOG:=/tmp/winuae_unix_p96_guest_z3_smoke.log}"
export WINUAE_P96_Z3 WINUAE_SMOKE_LOG

exec "$(dirname -- "$0")/unix-smoke-p96-guest.sh" "$@"
