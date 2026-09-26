#!/usr/bin/env bash
# Fails if a configure or build log holds any warning, third-party code's
# included (-Werror only covers the engine's own code; see the top-level
# CMakeLists.txt). Every warning gets fixed at its cause, never silenced.
#
#   tools/ci/check_warnings.sh configure.log build.log ...
set -euo pipefail
[ $# -gt 0 ] || { echo "usage: $0 <log>..." >&2; exit 2; }
found=0
for log in "$@"; do
    [ -f "$log" ] || { echo "check_warnings.sh: '$log' does not exist" >&2; exit 2; }
    # Compiler/linker warnings, CMake warnings, and wayland-scanner's DTD
    # banner (it prints no "warning:" prefix).
    hits="$(grep -nE 'warning:|CMake Warning|CMake Deprecation Warning|XML failed validation' "$log" || true)"
    if [ -n "$hits" ]; then
        echo "::error title=Warnings in $(basename "$log")::$(printf '%s\n' "$hits" | wc -l) warning line(s); see the log below"
        printf '%s\n' "$hits" | head -50
        found=1
    fi
done
[ "$found" -eq 0 ] && echo "no warnings in: $*"
exit "$found"
