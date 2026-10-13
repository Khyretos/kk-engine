#!/usr/bin/env bash
# Runs kk-engine's unit tests on the Mac CI runner (runs-on: macos-amd64, .forgejo/workflows/macos.yml)
# from the x86_64 package and kke_tests that soucouyant cross-built with osxcross, building nothing.
# The source tree is unpacked at /tmp/kke-src, the path the build used, because some tests read shaders, scenes and fonts from KKE_SOURCE_DIR;
# Vulkan is MoltenVK from the runner's $VULKAN_SDK.
#
# Usage: tools/ci/macos_test.sh <package.tar.gz> <kke_tests> <src.tar> <work> <results>
#
# Output: kke_tests.json and kke_tests.log go to <results>.

set -euo pipefail

die() {
	echo "macos_test.sh: error: $*" >&2
	exit 1
}

if [ $# -ne 5 ]; then
	echo "Usage: tools/ci/macos_test.sh <package.tar.gz> <kke_tests> <src.tar> <work> <results>" >&2
	exit 2
fi

package=$1
tests=$2
srctar=$3
work=$4
results=$5
src=/tmp/kke-src

rm -rf "$work" "$src"
mkdir -p "$work" "$src" "$results"
results="$(cd "$results" && pwd)"

tar -xzf "$package" -C "$work"
pkg="$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [ -z "$pkg" ]; then
	die "$package holds no folder"
fi

for f in kke_demo shaders assets benchmark/kke_benchmark; do
	if [ ! -e "$pkg/$f" ]; then
		die "$f is missing from $package"
	fi
done

cp "$tests" "$pkg/kke_tests"
chmod +x "$pkg/kke_tests"

tar -xf "$srctar" -C "$src"
if [ ! -d "$src/shaders" ]; then
	die "no shaders/ folder in $srctar"
fi

# Remove quarantine attribute if present (e.g. from browser/Finder)
xattr -dr com.apple.quarantine "$pkg" 2>/dev/null || true

# volk opens libvulkan.dylib at run time; macOS drops DYLD_* variables when it starts a system program such as /bin/bash,
# so a value from the workflow's env never arrives and it is set here, just before kke_tests.
export DYLD_FALLBACK_LIBRARY_PATH="${VULKAN_SDK:+$VULKAN_SDK/lib:}/usr/local/lib:/usr/lib"

cd "$pkg"
status=0
./kke_tests --gtest_brief=1 "--gtest_output=json:$results/kke_tests.json" >"$results/kke_tests.log" 2>&1 || status=$?
tail -n 15 "$results/kke_tests.log"
echo "kke_tests exit code $status"
exit "$status"
