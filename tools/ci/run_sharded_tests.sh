#!/usr/bin/env bash
# Runs a GoogleTest binary as N shards in parallel (GTEST_TOTAL_SHARDS /
# GTEST_SHARD_INDEX), for the coverage run, where -O0 plus gcov counters made
# one process take 13 minutes:
#
#   tools/ci/run_sharded_tests.sh <kke_tests> <shards> [gtest args...]
#
# GoogleTest splits the same test list over the shards, every test in exactly
# one of them; this also checks that the shards together ran as many tests as
# the binary lists. Coverage stays whole: libgcov locks each .gcda file while
# it merges a process's counters into it. Each shard gets its own TMPDIR and
# XDG folders, since tests of one fixture (same temp folder name) may now run
# at the same time in two processes. A shard's output is printed in full when
# it fails, otherwise only its summary.
set -uo pipefail

[ $# -ge 2 ] || { echo "usage: $0 <test binary> <shards> [gtest args...]" >&2; exit 2; }
bin=$(realpath "$1")
shards=$2
shift 2

# Listed tests minus DISABLED_ ones, which GoogleTest lists but doesn't run.
expected=$("$bin" --gtest_list_tests "$@" | awk '
  /^[^ ]/ { suite = $1; next }
  suite !~ /^DISABLED_/ && $1 !~ /^DISABLED_/ { n++ }
  END { print n + 0 }')
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

pids=()
for ((i = 0; i < shards; i++)); do
  dir="$work/shard$i"
  mkdir -p "$dir/tmp" "$dir/cache" "$dir/config" "$dir/data"
  GTEST_TOTAL_SHARDS=$shards GTEST_SHARD_INDEX=$i TMPDIR="$dir/tmp" \
    XDG_CACHE_HOME="$dir/cache" XDG_CONFIG_HOME="$dir/config" XDG_DATA_HOME="$dir/data" \
    "$bin" "$@" > "$dir/log" 2>&1 &
  pids+=($!)
done

failed=0
ran=0
for ((i = 0; i < shards; i++)); do
  wait "${pids[$i]}"
  code=$?
  log="$work/shard$i/log"
  n=$(grep -oP '^\[==========\] \K\d+(?= tests? from .* ran\.)' "$log" || echo 0)
  ran=$((ran + n))
  if [ "$code" -ne 0 ]; then
    echo "== shard $i/$shards: FAILED (exit $code)"
    cat "$log"
    failed=1
  else
    echo "== shard $i/$shards: $n tests, $(grep -E '^\[  PASSED  \]' "$log")"
  fi
done

echo "$ran of $expected tests ran in $shards shards"
if [ "$ran" -ne "$expected" ]; then
  echo "::error title=Sharded tests::$ran tests ran, the binary lists $expected"
  failed=1
fi
exit $failed
