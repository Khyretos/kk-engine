# Lesson: "another player's jump moved me" in a networked race (kk-engine)

Task: Kees saw the host's jump slightly move his climber in Climb Race online.
Result: commit f2315bc on main (2026-10-04). Two causes fixed.

## What went wrong, and why

1. **Shared stateful helper (the real cause).** `ClimbRaceModule` had ONE
   `kke::FootPlacer m_feet` and called `m_feet.apply(...)` for every climber
   in a loop. FootPlacer keeps smoothed values between frames
   (`m_footOffset`, `m_pelvisOffset`, `m_footNormal`). When one climber
   jumped, its feet reached for the ground below and those smoothed values
   moved; the next climber in the loop then used them, so its hips dipped
   up to 5.4 cm. Nothing to do with networking or physics: it also happens
   offline with CPU climbers.
   Fix: give each racer its own copy (`Racer::feet = m_feet` when the body
   is set up; call `r.feet.apply`). Rule: **any object that remembers
   values from the previous frame (smoothing, filters, springs, timers)
   must belong to ONE character.** Setup data (bone chains, rest poses)
   may be shared.
2. **Kinematic teleport sweep (racing).** A remote car's solid copy was
   moved with `moveKinematic` every frame. When its owner reset it on the
   track, the copy crossed the gap in one step at huge speed with infinite
   mass and hit local cars. Fix: if the jump is longer than
   `max(5 m, speed * 0.5 s)`, use `setTransform` (teleport) and reset the
   draw interpolation (`prevXf = xf`).

## What did NOT cause it (checked, so don't re-check)

- Jolt characters do not collide with each other: `CharacterVirtual` has no
  `CharacterVsCharacterCollision` set, and its inner body is on a layer
  only cloth sees (`engine/src/RigidWorld.cpp` Layers/ObjectPairs).
- Climb Race sets `NetModule::standIns = false`, so no remote capsules.
- No `onCorrection` handler in Climb Race, so host corrections do nothing.

## How it was found (the method that worked)

Reading code found nothing. Measuring did:
1. Add a TEMPORARY log line printing each racer's position (and later the
   foot placer's pelvis offset) every frame, behind an env var.
2. Run host and client headless, make the host jump, and check whether the
   client's idle racer changes. Position: unchanged. Pelvis offset: dipped
   to -0.054 at every host jump. That pointed straight at FootPlacer.
3. After the fix the same run showed pelvis 0.0000 while the host jumped
   (airborne in 114 frames).
4. Remove the temporary logs before committing (`git checkout` the file or
   delete the lines; check `git diff`).

## Exact commands (all run and verified in the cloud sandbox, Ubuntu 24.04)

Dependencies (lua.org, sqlite.org, freedesktop are blocked there):
```
curl -sSfLO http://archive.ubuntu.com/ubuntu/pool/main/l/lua5.4/lua5.4_5.4.7.orig.tar.gz
curl -sSfLO http://archive.ubuntu.com/ubuntu/pool/main/w/wayland/wayland_1.26.0.orig.tar.xz
npm pack better-sqlite3@13.0.3   # then: patch -R -p1 < deps/patches/1208.patch inside package/
```
Configure and build only what you need (about 15 min on 4 cores):
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_FETCH_SKIES=OFF -DKKE_WARNINGS_AS_ERRORS=ON \
  -DFETCHCONTENT_SOURCE_DIR_LUA=$D/lua -DFETCHCONTENT_SOURCE_DIR_SQLITE=$D/bs/package/deps/sqlite3 \
  -DFETCHCONTENT_SOURCE_DIR_WAYLAND_SCANNER_SRC=$D/wayland
cmake --build build --target climb_race racing flying_demo party
```
Two-player headless test (two virtual screens):
```
Xvfb :99 -screen 0 1280x720x24 &   Xvfb :98 -screen 0 1280x720x24 &
export KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0
DISPLAY=:99 KKE_NET=host KKE_CLIMB_WAIT=1 KKE_NET_NAME=Host timeout 50 build/bin/climb_race > host.log 2>&1 &
sleep 4
DISPLAY=:98 KKE_NET=join:127.0.0.1 KKE_LOBBY_JOIN=1 KKE_NET_NAME=Client timeout 46 build/bin/climb_race > client.log 2>&1 &
W=$(DISPLAY=:99 xdotool search --name "Climb Race" | head -1)
DISPLAY=:99 xdotool windowraise $W windowfocus --sync $W
DISPLAY=:99 xdotool keydown space; sleep 0.3; DISPLAY=:99 xdotool keyup space   # host jumps
```
Racing uses `KKE_RACE_WAIT=1` instead of `KKE_CLIMB_WAIT`.
Clang warning check (Android CI uses clang, local Linux uses GCC): take the
file's command from `ninja -t commands`, swap the compiler for clang++,
drop `-o` and `-MD -MT -MF`, add `-fsyntax-only -Wall -Wextra -Werror`.

## Common errors and fixes

- `KKE_MAIN_MENU` not set to 0: the title menu blocks `KKE_NET`, nothing joins.
- `pkill -f bin/climb_race` inside a script killed the script itself (exit
  144). Use `pkill -x climb_race`.
- Jolt `CharacterID` has `IsInvalid()`, not `IsValid()`.
- "XDG_RUNTIME_DIR is invalid" in headless logs is the sandbox's audio, not a game bug.
- `grep -i error` on the CMake configure log matches "OPUS_ASSERTIONS, additional
  software error checking": wait for the build's own exit code instead.

## Could a 9B local model do this alone?

Partly. The fix itself (copy one member into the per-player struct, change
one call) is easy for a 9B model once the cause is named. Finding the cause
is the hard part: the obvious suspects (physics collisions, net corrections)
were all clean, and only measuring revealed it. A 9B model would need:
- this lesson's rule ("frame-to-frame state must belong to one character")
  as a checklist item, plus a grep: module members of types like FootPlacer,
  LookAt, ProceduralGait, CameraRig used inside loops over players;
- the two-window test script above, ready to run, and the instruction to add
  a temporary per-frame log and compare idle-player values;
- small steps: build, run, read 20 log lines, decide; not "find the bug".
