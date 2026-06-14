#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <qemu-uae-plugin>" >&2
    exit 2
fi

plugin=$1

if [ ! -f "$plugin" ]; then
    echo "error: Unix PPC support is enabled, but no qemu-uae.so" \
        "plugin is available for packaging." >&2
    echo "error: expected plugin: $plugin" >&2
    echo "error: set WINUAE_QEMU_UAE_PLUGIN_FILE or build the" \
        "winuae_unix_qemu_uae_plugin target." >&2
    exit 1
fi
