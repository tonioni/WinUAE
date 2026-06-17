#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: $0 <target-dir> <sdk-url> <sdk-version> <sha256>" >&2
    exit 2
fi

target_dir=$1
sdk_url=$2
sdk_version=$3
sdk_sha256=$4

if [ -f "$target_dir/C/7z/7zVersion.h" ] ||
    [ -f "$target_dir/7z/7zVersion.h" ]; then
    exit 0
fi

cache_parent=$(dirname "$target_dir")
mkdir -p "$cache_parent"

tmp_dir=$(mktemp -d "$cache_parent/.fetch-$sdk_version.XXXXXX")
cleanup()
{
    rm -rf "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM

archive_path="$tmp_dir/lzma-sdk.7z"
extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir"

if command -v curl >/dev/null 2>&1; then
    curl -fL "$sdk_url" -o "$archive_path"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$archive_path" "$sdk_url"
else
    echo "curl or wget is required to fetch $sdk_url" >&2
    exit 1
fi

if [ -n "$sdk_sha256" ]; then
    if command -v sha256sum >/dev/null 2>&1; then
        archive_sha256=$(sha256sum "$archive_path" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        archive_sha256=$(shasum -a 256 "$archive_path" | awk '{print $1}')
    else
        echo "sha256sum or shasum is required to verify $sdk_url" >&2
        exit 1
    fi
    if [ "$archive_sha256" != "$sdk_sha256" ]; then
        echo "downloaded SDK checksum mismatch for $sdk_url" >&2
        echo "expected: $sdk_sha256" >&2
        echo "actual:   $archive_sha256" >&2
        exit 1
    fi
fi

extract_archive()
{
    if command -v cmake >/dev/null 2>&1; then
        if (cd "$extract_dir" && cmake -E tar xf "$archive_path")
        then
            return 0
        fi
    fi
    if command -v 7zz >/dev/null 2>&1; then
        if 7zz x -y "-o$extract_dir" "$archive_path"; then
            return 0
        fi
    fi
    if command -v 7z >/dev/null 2>&1; then
        if 7z x -y "-o$extract_dir" "$archive_path"; then
            return 0
        fi
    fi
    if command -v 7za >/dev/null 2>&1; then
        if 7za x -y "-o$extract_dir" "$archive_path"; then
            return 0
        fi
    fi
    if command -v bsdtar >/dev/null 2>&1; then
        if (cd "$extract_dir" && bsdtar -xf "$archive_path"); then
            return 0
        fi
    fi
    return 1
}

if ! extract_archive; then
    echo "could not extract $archive_path; install cmake, 7zz, 7z, 7za, or bsdtar" >&2
    exit 1
fi

version_header=
for candidate in \
    "$extract_dir/C/7z/7zVersion.h" \
    "$extract_dir/C/7zVersion.h" \
    "$extract_dir/7z/7zVersion.h" \
    "$extract_dir/7zVersion.h"
do
    if [ -f "$candidate" ]; then
        version_header=$candidate
        break
    fi
done

if [ -z "$version_header" ]; then
    version_header=$(find "$extract_dir" -path '*/C/7z/7zVersion.h' -print -quit)
fi
if [ -z "$version_header" ]; then
    version_header=$(find "$extract_dir" -path '*/C/7zVersion.h' -print -quit)
fi
if [ -z "$version_header" ]; then
    version_header=$(find "$extract_dir" -path '*/7z/7zVersion.h' -print -quit)
fi
if [ -z "$version_header" ]; then
    version_header=$(find "$extract_dir" -path '*/7zVersion.h' -print -quit)
fi
if [ -z "$version_header" ]; then
    echo "downloaded SDK does not contain 7z/7zVersion.h" >&2
    exit 1
fi

if ! grep -F "MY_VERSION \"$sdk_version\"" "$version_header" >/dev/null; then
    echo "downloaded SDK is not version $sdk_version: $version_header" >&2
    exit 1
fi

case "$version_header" in
    */C/7z/7zVersion.h)
        sdk_root=${version_header%/C/7z/7zVersion.h}
        ;;
    */C/7zVersion.h)
        sdk_root=${version_header%/C/7zVersion.h}
        ;;
    */7z/7zVersion.h)
        sdk_root=${version_header%/7z/7zVersion.h}
        ;;
    */7zVersion.h)
        sdk_root=${version_header%/7zVersion.h}
        ;;
    *)
        echo "unexpected SDK layout for $version_header" >&2
        exit 1
        ;;
esac

if [ -f "$sdk_root/C/7zVersion.h" ]; then
    mkdir -p "$sdk_root/C/7z"
    find "$sdk_root/C" -maxdepth 1 -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.rc' \) \
        -exec mv {} "$sdk_root/C/7z/" \;
elif [ -f "$sdk_root/7zVersion.h" ]; then
    mkdir -p "$sdk_root/7z"
    find "$sdk_root" -maxdepth 1 -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.rc' \) \
        -exec mv {} "$sdk_root/7z/" \;
fi

if [ ! -f "$sdk_root/C/7z/7zVersion.h" ] &&
    [ ! -f "$sdk_root/7z/7zVersion.h" ]; then
    echo "could not normalize SDK layout under $sdk_root" >&2
    exit 1
fi

target_tmp="$target_dir.tmp.$$"
rm -rf "$target_tmp"
mv "$sdk_root" "$target_tmp"
rm -rf "$target_dir"
mv "$target_tmp" "$target_dir"
