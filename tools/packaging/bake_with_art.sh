#!/usr/bin/env bash
# The old name of a private bake with Synty art. It now runs tools/bake,
# which does every platform in one go (docs/BAKING.md):
#
#   tools/bake --assets DIR                     # Linux, AppImage, Windows, Android, all with art
#
# These flags still work and mean what they did:
#
#   tools/packaging/bake_with_art.sh --assets DIR [--sprites DIR] [--version NAME] [--windows] [--android] [--all-art]
#
# makes the Linux .tar.gz, plus the Windows zip with --windows and the phone
# APKs with --android. --build DIR is accepted and ignored (the builds live
# in build-docker/ now).
set -euo pipefail

only="linux" args=()
while [ $# -gt 0 ]; do
	case "$1" in
	--windows)
		only="$only,windows"
		shift
		;;
	--android)
		only="$only,android"
		shift
		;;
	--build)
		echo "bake_with_art.sh: note: --build is no longer used (builds live in build-docker/)"
		shift 2
		;;
	--assets | --sprites | --version)
		args+=("$1" "$2")
		shift 2
		;;
	--all-art)
		args+=("$1")
		shift
		;;
	*)
		echo "bake_with_art.sh: error: unknown argument '$1' (see the top of this script)" >&2
		exit 1
		;;
	esac
done
[ -n "${KKE_ASSETS_DIR:-}" ] || printf '%s\n' "${args[@]}" | grep -qx -- --assets || {
	echo "bake_with_art.sh: error: --assets <your Synty packs folder> (or set KKE_ASSETS_DIR)" >&2
	exit 1
}
exec "$(dirname "$0")/../bake" --only "$only" "${args[@]}"
