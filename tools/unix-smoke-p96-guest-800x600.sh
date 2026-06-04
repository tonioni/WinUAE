#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WINUAE_P96_SCREENMODE=${WINUAE_P96_SCREENMODE:-800x600x8}
WINUAE_SMOKE_LOG=${WINUAE_SMOKE_LOG:-/tmp/winuae_unix_p96_guest_800x600_smoke.log}
export WINUAE_P96_SCREENMODE WINUAE_SMOKE_LOG

exec "$SCRIPT_DIR/unix-smoke-p96-guest.sh" "$@"
