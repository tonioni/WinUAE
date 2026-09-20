#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <floppysounds-dir>" >&2
    exit 2
fi

sample_dir=$1

if [ ! -d "$sample_dir" ]; then
    echo "error: drive sound sample directory not found: ${sample_dir}" >&2
    exit 1
fi

found_click=false
for sample in "$sample_dir"/drive_click_*.wav; do
    [ -f "$sample" ] || continue
    found_click=true
    if [ ! -r "$sample" ] || [ ! -s "$sample" ]; then
        echo "error: unreadable or empty drive sound sample: ${sample}" >&2
        exit 1
    fi
done

if [ "$found_click" != true ]; then
    echo "error: no readable drive_click_*.wav sample sets found in ${sample_dir}" >&2
    exit 1
fi

for sample in "$sample_dir"/*.wav; do
    [ -f "$sample" ] || continue
    if [ ! -r "$sample" ] || [ ! -s "$sample" ]; then
        echo "error: unreadable or empty drive sound sample: ${sample}" >&2
        exit 1
    fi
done
