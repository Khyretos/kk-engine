#!/usr/bin/env bash
# Copies Xelu's Free Controller & Key Prompts (CC0, see
# assets/prompts/xelu/LICENSE.txt) into assets/prompts/xelu/ with plain
# folder names (no spaces or "&"), keeping every PNG's own file name.
# The Flash sources (*.fla) and the 256 px keyboard export zip are left
# out: the engine uses the 100 px PNGs.
#
#   tools/prompts/import_xelu.sh "/path/to/Xelu_Free_Controller&Key_Prompts"
#
# (tools/fetch_assets.sh Xelu extracts the pack from the asset share.)
set -euo pipefail

SRC="${1:?usage: import_xelu.sh PACK_FOLDER}"
DEST="$(cd "$(dirname "$0")/../.." && pwd)/assets/prompts/xelu"
[[ -f "$SRC/Readme.txt" ]] || {
	echo "import_xelu.sh: $SRC has no Readme.txt; is it the Xelu pack?" >&2
	exit 2
}

# "Others/Amazon Luna/Luna_A.png" -> "others/amazon_luna/Luna_A.png"
folder() { tr '[:upper:]' '[:lower:]' <<<"$1" | sed -E 's/ & /_/g; s/[ -]+/_/g'; }

rm -rf "$DEST"
mkdir -p "$DEST"
cp "$SRC/Readme.txt" "$DEST/LICENSE.txt"
declare -A renamed=(
	["Keyboard & Mouse/Dark"]="keyboard_dark"
	["Keyboard & Mouse/Light"]="keyboard_light"
	["Keyboard & Mouse/Blanks"]="keyboard_blanks"
)
count=0
while IFS= read -r -d '' png; do
	rel="${png#"$SRC"/}"
	dir="$(dirname "$rel")"
	out="${renamed[$dir]:-$(folder "$dir")}"
	mkdir -p "$DEST/$out"
	cp "$png" "$DEST/$out/$(basename "$png")"
	count=$((count + 1))
done < <(find "$SRC" -type f -name '*.png' -print0 | sort -z)
echo "import_xelu.sh: $count glyphs -> $DEST"
