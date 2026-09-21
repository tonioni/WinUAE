#!/usr/bin/env sh
set -eu

plugin=${1:-}
if [ -z "${plugin}" ]; then
    echo "usage: $0 <FloppyBridge-plugin>" >&2
    exit 2
fi
if [ ! -f "${plugin}" ]; then
    echo "error: FloppyBridge support is enabled, but FloppyBridge.so is missing: ${plugin}" >&2
    exit 1
fi
