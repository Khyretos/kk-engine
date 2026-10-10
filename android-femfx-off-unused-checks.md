# android-femfx-off-unused: checks

Base: Forgejo main bcc21ec (via GitHub mirror). Branch name suggestion: `fix/android-unused-femfx-off`.

## Why
GitHub mirror CI run 37885493222 (bcc21ec), job `android`, fails with:
`games/showcase/Range.cpp:45:35: error: unused variable 'kLaneB' [-Werror,-Wunused-const-variable]`.
A clang `-fsyntax-only -Werror` pass over every games/ and engine/src TU (FEMFX off, same as Android) also shows
`games/sea_demo/SeaDemoModule.cpp:945:13: error: lambda capture 'this' is not used [-Werror,-Wunused-lambda-capture]`.
The Android build stopped at the first failure, so that one is next in line.

## Checked in the cloud
- Both files: clang++ and g++ with `-Wall -Wextra -Werror`, FEMFX off, give no warnings.
- Clang -Werror pass over all 366 games/ + engine/src TUs on main: the two errors above are the only ones.
- Debug build of main (GCC 13) has 0 warnings. kke_tests: 1016 passed, 22 skipped (servers/asset packs), 0 failed.
- Not checked: the FEMFX-on build. The FEMFX headers need their CMake defines. That path keeps the same lambda body and the same kLaneB value.

## Still to check on soucouyant
- Build with the everything preset (FEMFX on) plus an android-arm64 build: 0 warnings.
- Run kke_demo (Range) and sea_demo (fort panel text) briefly.
