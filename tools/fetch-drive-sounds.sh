#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <target-dir> <archive-url> <sha256>" >&2
    exit 2
fi

target_dir=$1
archive_url=$2
archive_sha256=$(printf '%s' "$3" | tr '[:upper:]' '[:lower:]')
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "$target_dir" in
    ''|/|.)
        echo "error: refusing unsafe drive sound target directory: ${target_dir}" >&2
        exit 1
        ;;
esac

sample_target="${target_dir}/plugins/floppysounds"

cache_parent=$(dirname "$target_dir")
mkdir -p "$cache_parent"

tmp_dir=$(mktemp -d "${cache_parent}/.fetch-drive-sounds.XXXXXX")
cleanup()
{
    rm -rf "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM

archive_path="${tmp_dir}/drive_sounds.zip"
extract_dir="${tmp_dir}/extract"
mkdir -p "$extract_dir"

if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 "$archive_url" -o "$archive_path"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$archive_path" "$archive_url"
else
    echo "error: curl or wget is required to fetch ${archive_url}" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    actual_sha256=$(sha256sum "$archive_path" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then
    actual_sha256=$(shasum -a 256 "$archive_path" | awk '{print $1}')
else
    echo "error: sha256sum or shasum is required to verify ${archive_url}" >&2
    exit 1
fi

if [ "$actual_sha256" != "$archive_sha256" ]; then
    echo "error: drive sound archive checksum mismatch for ${archive_url}" >&2
    echo "expected: ${archive_sha256}" >&2
    echo "actual:   ${actual_sha256}" >&2
    exit 1
fi

extracted=
if command -v cmake >/dev/null 2>&1; then
    if (cd "$extract_dir" && cmake -E tar xf "$archive_path"); then
        extracted=1
    fi
fi
if [ -z "$extracted" ] && command -v unzip >/dev/null 2>&1; then
    if unzip -q "$archive_path" -d "$extract_dir"; then
        extracted=1
    fi
fi
if [ -z "$extracted" ]; then
    echo "error: cmake or unzip is required to extract ${archive_path}" >&2
    exit 1
fi

"${script_dir}/require-drive-sounds.sh" \
    "${extract_dir}/plugins/floppysounds"
printf '%s\n' "$actual_sha256" \
    > "${extract_dir}/plugins/floppysounds/.archive-sha256"

sample_parent="${target_dir}/plugins"
sample_tmp="${sample_target}.tmp.$$"
mkdir -p "$sample_parent"
rm -rf "$sample_tmp"
mv "${extract_dir}/plugins/floppysounds" "$sample_tmp"
rm -rf "$sample_target"
mv "$sample_tmp" "$sample_target"
