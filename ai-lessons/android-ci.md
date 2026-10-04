# Lesson: fixing kk-engine's red Android CI job (2026-10-04)

## What broke and why

- CI job `android` (`.github/workflows/ci.yml`), step "Build (android-arm64)", failed on every
  main run from 2026-10-03 ~16:04 UTC.
- Error (from the job log):
  `games/party/minigames/ObstacleCourse.cpp:34:37: error: unused variable 'kHammerZ1' [-Werror,-Wunused-const-variable]`
- Cause: commit c4fac9c changed `floor(kGapZ1 - 1.0f, kHammerZ1 - 2.0f)` to end at the disc
  instead, which removed the only use of the constant `kHammerZ1`.
- Why nobody saw it: local builds and the Linux CI jobs use GCC. GCC does not warn about an
  unused `constexpr` at namespace scope; clang does (`-Wunused-const-variable` is in `-Wall`
  for clang). The Android job uses the NDK's clang with `-Werror`, so the warning is an error.
- Fix (commit 9d2b496): delete the dead constant. Do not add `[[maybe_unused]]`, `(void)x`
  or `-Wno-...`: the project rule is to fix warnings, never silence them.

## Steps that worked (all verified in the cloud container, 4 cores)

1. Read the failing log. With the GitHub MCP:
   `actions_list` (method list_workflow_runs, resource_id ci.yml, branch main), then
   `get_job_logs` with the run id, `failed_only: true`, `tail_lines: 80`.
   Search the log for `error:` and `FAILED:`.
2. Remember: ninja stops at the first failure, so ONE error in the log does not mean only one
   problem. Build everything locally with `-k 0` to see all of them.
3. Get NDK r28c (exact version CI uses, 28.2.13676358). It is a 722 MB zip:
   ```
   curl -sSL -o ndk.zip https://dl.google.com/android/repository/android-ndk-r28c-linux.zip
   unzip -q ndk.zip    # gives android-ndk-r28c/
   ```
4. In the cloud sandbox only, lua.org and sqlite.org are blocked. Mirrors that work:
   - Lua: `http://archive.ubuntu.com/ubuntu/pool/main/l/lua5.4/lua5.4_5.4.7.orig.tar.gz`
     (extract to `lua-5.4.7/`)
   - SQLite: `npm pack better-sqlite3@13.0.3`, extract, `cd package`,
     `patch -R -p1 < deps/patches/1208.patch`, use `package/deps/sqlite3`.
   On Kees's PCs these are not needed (no proxy).
5. Configure exactly like CI, plus the mirror overrides:
   ```
   ANDROID_NDK_HOME=$PWD/../android-ndk-r28c cmake --preset android-arm64 \
     -DKKE_WARNINGS_AS_ERRORS=ON -DKKE_FETCH_SKIES=OFF \
     -DFETCHCONTENT_SOURCE_DIR_LUA=/path/lua-5.4.7 \
     -DFETCHCONTENT_SOURCE_DIR_SQLITE=/path/package/deps/sqlite3
   ```
   (configure took ~73 s)
6. Build and keep going past errors:
   ```
   cmake --build build-android-arm64 -j4 -- -k 0 > build.log 2>&1
   grep -E "FAILED|warning:|error:" build.log
   ```
   Result after the fix: `[1507/1507] Linking ... libcookbook.so`, zero matches. On soucouyant
   (32 GB RAM) keep `-j` capped (e.g. `-j6`); `-j` with no number can run out of memory.
7. Fetch + merge main, then push straight to main (Kees's rule: no PRs):
   `git fetch origin main && git merge origin/main && git push origin HEAD:main`.

## Common clang-only errors in this repo and their fixes

| Error | Fix |
|---|---|
| `unused variable 'X' [-Wunused-const-variable]` | Delete the constant, or use it if the code really needs it. Check `git log -p -- FILE` to see which commit removed its last use. |
| `private field 'm_x' is not used [-Wunused-private-field]` | Delete the field (GCC never warns about this). |
| `unused function [-Wunused-function]` in an anonymous namespace | Delete the function. |
| `std::atomic_ref` not found | libc++ in r28c lacks it; use `std::atomic<T>` as the member instead. |
| Missing `#include <...>` that GCC pulled in indirectly | Add the right header; `tools/ci/check_std_includes.py` also checks this. |

Quick check without the NDK: host `clang++ -fsyntax-only -Wall -Wextra -Werror` with the flags
from `ninja -C build -t commands` catches most of these, but not code under
`KKE_PLATFORM_ANDROID`. The NDK build is the real test.

## Could a 9B local model do this alone?

Mostly yes, with help:
- Easy parts: read the log, find `error:`, delete one line. A 9B model handles this if it is
  given the log tail and the file.
- Hard parts: (a) knowing that ninja stops early, so one error is not the whole picture;
  (b) knowing the fix must be removal, not suppression; (c) the long NDK download/configure.
- What it needs: this lesson, the exact commands above as a script (no guessing flags or
  versions), and a rule "after the fix, run the full `-k 0` build and require zero
  `warning:`/`error:` lines before pushing". Better: run the Android build on the soucouyant
  Forgejo runner for every push so the model only has to read its output.
- Risk: small models tend to "fix" warnings with `[[maybe_unused]]` or `#pragma`. The reviewer
  must reject that.
