#!/usr/bin/env bash
# Downloads art packs from Kees's asset share and extracts them where the
# engine looks for them (assets/synty/<Pack>/, git-ignored). Packs are
# licensed per user: never commit what this fetches (see assets/README.md).
#
#   tools/fetch_assets.sh --list                     # what the share holds
#   tools/fetch_assets.sh POLYGON_Town ANIMATION_    # every archive whose name
#                                                    # starts with one of these
#   KKE_ASSETS_DIR=/data/packs tools/fetch_assets.sh POLYGON_Fantasy_Characters
#
# One zip is downloaded at a time and deleted right after extracting, so
# the disk only needs room for the extracted packs plus the largest zip.
# Packs already extracted (a folder of that name exists) are skipped.
# .unitypackage archives are tar.gz with GUID-named entries; they are
# unpacked to their real asset paths.
#
# The share's link hash is private (anyone with it can download the paid
# packs), so it is never in the repo: set KKE_SHARE_HASH in your shell or
# in the environment's secrets.
set -euo pipefail

SHARE_HOST="${KKE_SHARE_HOST:-https://media.kreative-kompas.com/files}"
SHARE_HASH="${KKE_SHARE_HASH:-}"
if [[ -z "$SHARE_HASH" ]]; then
    echo "fetch_assets.sh: set KKE_SHARE_HASH to the asset share's link hash (kept out of the repo)" >&2
    exit 2
fi
DEST="${KKE_ASSETS_DIR:-$(cd "$(dirname "$0")/.." && pwd)/assets/synty}"

listing() {
    curl -fsS "$SHARE_HOST/public/api/resources?hash=$SHARE_HASH&path=/" |
        python3 -c 'import json,sys
for f in json.load(sys.stdin)["files"]:
    if f["type"] != "directory": print(str(f["size"]) + "\t" + f["name"])'
}

urlencode() { python3 -c 'import sys,urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"; }

unpack_unitypackage() { # $1 = .unitypackage, $2 = destination folder
    local tmp; tmp="$(mktemp -d)"
    tar -xzf "$1" -C "$tmp"
    for entry in "$tmp"/*/; do
        [[ -f "$entry/pathname" && -f "$entry/asset" ]] || continue
        local rel; rel="$(head -n1 "$entry/pathname" | tr -d '\r')"
        mkdir -p "$2/$(dirname "$rel")"
        mv "$entry/asset" "$2/$rel"
    done
    rm -rf "$tmp"
}

if [[ $# -eq 0 || "$1" == "--help" ]]; then
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
fi
if [[ "$1" == "--list" ]]; then
    listing | awk -F'\t' '{ printf "%8.1f MB  %s\n", $1 / 1048576, $2 }'
    exit 0
fi

mkdir -p "$DEST"
mapfile -t names < <(listing | cut -f2)
for prefix in "$@"; do
    matched=0
    for name in "${names[@]}"; do
        [[ "$name" == "$prefix"* ]] || continue
        matched=1
        # "POLYGON_Town_SourceFiles_v5.zip" -> "POLYGON_Town"
        pack="${name%.*}"
        pack="$(sed -E 's/[_ ]?(Source[_ ]?Files|SourceFiles|Source_Files|\[Source\]|\[Pro\]|Unity_20[0-9_]+|Unreal)[^/]*$//; s/ \([0-9]+\)$//' <<<"$pack")"
        [[ -n "$pack" ]] || pack="${name%.*}"
        if [[ -d "$DEST/$pack" ]]; then echo "skip   $pack (already in $DEST)"; continue; fi
        echo "fetch  $name -> $DEST/$pack"
        tmpfile="$(mktemp --suffix="-${name// /_}")"
        curl -fsS --retry 4 --retry-delay 2 -o "$tmpfile" "$SHARE_HOST/public/api/raw?hash=$SHARE_HASH&file=/$(urlencode "$name")"
        mkdir -p "$DEST/$pack.partial"
        case "$name" in
            *.zip) unzip -q -o "$tmpfile" -d "$DEST/$pack.partial" ;;
            *.unitypackage) unpack_unitypackage "$tmpfile" "$DEST/$pack.partial" ;;
            *) cp "$tmpfile" "$DEST/$pack.partial/$name" ;;
        esac
        rm -f "$tmpfile"
        mv "$DEST/$pack.partial" "$DEST/$pack"
    done
    [[ $matched -eq 1 ]] || echo "no archive on the share starts with '$prefix' (try --list)" >&2
done
