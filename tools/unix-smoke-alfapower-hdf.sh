#!/usr/bin/env sh
set -eu

WINUAE_IDE_EXPANSION_BOARD=alfapower
export WINUAE_IDE_EXPANSION_BOARD
exec "$(dirname -- "$0")/unix-smoke-ide-expansion-hdf.sh"
