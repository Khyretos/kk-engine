#!/usr/bin/env bash
# Sets up a CI build job (.forgejo/workflows/*.yml), run after the checkout
# and the ccache restore:
#
#   tools/ci/build_env.sh <cache> [<cache>...]
#
# - KKE_JOBS: parallel compiles for this worker, min(CPUs, GB of free memory
#   / 2) (one GCC job on the engine peaks near 2 GB), and never below 3 where
#   the worker has 3 CPUs: the 3-CPU, 6 GB WSL VM built at -j 3 for weeks,
#   while -j 8 ran it out of memory. Container limits (cgroup v2) count.
# - ccache: installed if the image lacks it, put in front of every compiler
#   through CMake's CMAKE_<LANG>_COMPILER_LAUNCHER environment variables, and
#   pointed at .ccache/<cache> in the workspace, which actions/cache keeps
#   between runs. The first <cache> is the default; a build that needs
#   another (the coverage build) sets CCACHE_DIR=.ccache/<name> itself.
#   Sloppiness lets it cache files built with a precompiled header.
#   Statistics are zeroed here, so `ccache -s` after a build shows that run.
#
# Everything goes to $GITHUB_ENV for the job's later steps.
set -euo pipefail

[ $# -ge 1 ] || { echo "usage: $0 <cache> [<cache>...]" >&2; exit 2; }
: "${GITHUB_ENV:?not running in a CI job}"
ws="${GITHUB_WORKSPACE:-$PWD}"

# --- KKE_JOBS ---
cpus=$(nproc)
if [ -r /sys/fs/cgroup/cpu.max ]; then
  read -r quota period < /sys/fs/cgroup/cpu.max
  if [ "$quota" != max ]; then
    limit=$(( (quota + period - 1) / period ))
    [ "$limit" -lt "$cpus" ] && cpus=$limit
  fi
fi
mem_kb=$(awk '/^MemAvailable:/ { print $2 }' /proc/meminfo)
if [ -r /sys/fs/cgroup/memory.max ] && [ "$(cat /sys/fs/cgroup/memory.max)" != max ]; then
  free_kb=$(( ($(cat /sys/fs/cgroup/memory.max) - $(cat /sys/fs/cgroup/memory.current)) / 1024 ))
  [ "$free_kb" -lt "$mem_kb" ] && mem_kb=$free_kb
fi
mem_gb=$(( mem_kb / 1024 / 1024 ))
jobs=$(( mem_gb / 2 ))
[ "$jobs" -gt "$cpus" ] && jobs=$cpus
floor=$(( cpus < 3 ? cpus : 3 ))
[ "$floor" -lt 2 ] && floor=2
[ "$jobs" -lt "$floor" ] && jobs=$floor
echo "KKE_JOBS=$jobs ($cpus CPUs, $mem_gb GB free)"
echo "KKE_JOBS=$jobs" >> "$GITHUB_ENV"

# --- ccache ---
if ! command -v ccache > /dev/null; then
  sudo=""
  [ "$(id -u)" -ne 0 ] && sudo="sudo"
  echo "ccache is not in this image; installing it"
  $sudo apt-get update -qq
  DEBIAN_FRONTEND=noninteractive $sudo apt-get install -y -qq --no-install-recommends ccache > /dev/null
fi
ccache --version | head -n 1

{
  echo "CCACHE_DIR=$ws/.ccache/$1"
  echo "CCACHE_BASEDIR=$ws"
  echo "CCACHE_COMPILERCHECK=content"
  echo "CCACHE_MAXSIZE=2G"
  # SDL3 builds with a precompiled header; without these ccache can't cache
  # any file that uses it (654 of 1625 calls in a coverage build).
  echo "CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime"
  echo "CMAKE_C_COMPILER_LAUNCHER=ccache"
  echo "CMAKE_CXX_COMPILER_LAUNCHER=ccache"
} >> "$GITHUB_ENV"

for cache in "$@"; do
  mkdir -p "$ws/.ccache/$cache"
  CCACHE_DIR="$ws/.ccache/$cache" ccache --zero-stats > /dev/null
  echo "ccache $cache: $(CCACHE_DIR="$ws/.ccache/$cache" ccache --print-stats | awk -F'\t' '$1 == "files_in_cache" { print $2 }') files restored"
done
