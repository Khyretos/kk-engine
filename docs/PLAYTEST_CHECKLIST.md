# Playtest checklist — look and feel

One pass through every demo and system, done by a person at a real
machine. Each section says which project thread built it, how to start
it, and what to look for. Tick a box when it's right. When something is
wrong, leave the box empty and write what you saw on the **Fix:** line
(a screenshot helps). Send the file back, or paste the sections with
notes, and each note becomes a fix or a `BUGS.md` row.

This list is about **how things look, sound and feel**. Measurements
that need real hardware (benchmarks, thread scaling, min-spec, voice
echo) stay in [HARDWARE_TESTS.md](HARDWARE_TESTS.md); where a check
here overlaps one there, it links to it instead of repeating it.

**Before you start**

```bash
git pull
cmake --workflow --preset everything-release   # zero warnings expected
cd build-release/bin
```

Keep the terminal visible: a **warning or error in the log is a bug**
even when the game looks fine, so copy any `[warn]`/`[error]` line onto
the Fix line of the section you were in. Have a controller plugged in and try
it everywhere. Known gap: `sea_demo`, `melt_demo`, `jiggle_demo`,
`physics_demo`, `audio_demo` and `synty_demo` don't read a controller yet
(controller support for every demo is its own task next). Demos marked *(Synty)* need your packs in `KKE_ASSETS_DIR`
(or `assets/synty/`); without them they say so on screen and in the log.

---

## 0. Every demo (all threads)

Do these once per demo as you go through the list below.

- [ ] The Kreative Kompas logo intro plays, any key or button skips it.
- [ ] The log has no `warn` or `error` lines from start to quit.
- [ ] Closing the window (not Ctrl+C) quits cleanly, no crash output (HW-006).
- [ ] Resizing the window small and large keeps all text and panels on screen and readable.
- [ ] Button hints show the glyphs of the device you last touched (keyboard keys vs. controller buttons) and switch when you swap.
- [ ] The sky, light colour and background sound fit the demo (moods, below).

**Fix:**

## 1. Climb Race — `./climb_race`

*Threads: Climbing race demo, Climbing hands and how-to-play, Start menu
and joining players, Online play with local players, Climb Race flagship
game (its notes go to that thread).*

Start menu
- [ ] Climbers stand in front of the mountain, each above their player card.
- [ ] A second controller pressing A joins; keyboard joins with Enter when player 1 is on a pad.
- [ ] Name, colour, CPU count (0-5) and difficulty change and are remembered on the next start.
- [ ] The Mountain row lists the open mountains with best time and medal; locked ones aren't pickable.
- [ ] Menus are fully usable with only a controller (no mouse needed).

Climbing
- [ ] The how-to-play screen appears before the first race and X / H brings it back.
- [ ] Hands land on the holds (not floating beside them); fingers curl round the hold.
- [ ] Feet find footholds; hips move sensibly; no limb bends backwards.
- [ ] Hold colours match the how-to-play screen: green jug, orange crimp, blue sloper.
- [ ] Stamina drains and recovers as described; a lunge feels risky but useful.
- [ ] Loose rock falls believably and knocks a climber off.
- [ ] The finish (mantle over the summit) and results screen read clearly; the next mountain opens.
- [ ] CPU climbers of each difficulty feel different (Easy slow, Expert quick).
- [ ] Split screen with 2, 3 and 4 players: each view follows its climber; with 3 the fourth quarter shows the whole mountain.

Party modes and results (Mode row)
- [ ] Rockfall: rocks come at each climber more often as the leader climbs; a hit reads clearly and costs stamina.
- [ ] Elimination: every 30 s the lowest climber lets go; it's clear who is out and why.
- [ ] Time trial: the pale ghost repeats your best run; beating it replaces it.
- [ ] The results list (times or heights, medals, new bests, falls) is easy to read.
- [ ] Grabs, broken holds, falls, rocks, the countdown and medals each have a sound that fits.

Online (needs two PCs, or two copies on one PC)
- [ ] Host from the menu; the other copy finds it (LAN) or joins by address.
- [ ] Two local players on the joining PC both race online.
- [ ] Remote climbers' hands and feet look as good as local ones.
- [ ] Quitting the host shows "the host ended the game" on the client, not an error.

**Fix:**

## 2. Showcase — `./kke_demo`

*Threads: Continue kk-engine work, Lava station and wall run, Idle and
walk flicker, Networking v2, Game saves and voice polish, Assets, logo
intro and banner.*

- [ ] Walking, running (Shift), slow walk (Alt) and stopping: no flashing between idle and walk.
- [ ] Parkour lane: vault the fence and low wall, climb the block and the 2.1 m ledge, hang and shimmy on the 3 m wall, jump off.
- [ ] Wall run and ledge leaps feel responsive; landing never snaps or sinks into the ground.
- [ ] Lava station, pool and breaking yard work; the Synty art around them looks placed, not floating *(Synty)*.
- [ ] V switches first/third person; left click shoots; E pushes.
- [ ] Controller: every action above works from a pad, and the menu (Esc / Start) is usable with it.
- [ ] Split screen (`KKE_SPLIT=2 ./kke_demo`): the second player gets a pad and their own view.
- [ ] Multiplayer: `KKE_NET=host` on one copy, `KKE_NET=join:127.0.0.1` on another; both see each other move smoothly.
- [ ] F1 engine panels open, can be dragged, and close again.
- [ ] Voice chat echo/noise: see HW-018. The 1-core walk with sound: HW-019.

**Fix:**

## 3. Sandbox and Play mode — `./sandbox`

*Threads: Play-to-make (simple, nodes, Lua), Node graph editor, Lua v2
(audio v2, thumbnails), Physics bridge leftovers, Fracture interior fill,
Procedural animation (Shove / Look at blocks).*

Play mode (opens first)
- [ ] The row of big pictures is clear to a child: drag one out and it appears where you let go.
- [ ] Bat: swing at a person, they fall over (ragdoll) and Get up stands them back up.
- [ ] Throwing a ball, bonking, and breaking things all feel satisfying.
- [ ] Controller: the ring cursor, hold A to drag, LB/RB through the pictures, B cancels, Y stands everyone up.
- [ ] Touch (if you have a touchscreen): drag, tap, two-finger turn, pinch zoom (HW-016).

Node graph (the **Look** picture)
- [ ] Opening the bat's recipe shows *When someone is hit → Knock over → Play sound*.
- [ ] Editing it changes the bat straight away; blocks light up as they run.
- [ ] Show Lua shows readable code; the graph is still there after save and load.
- [ ] Shove makes a person stagger (light) or fall and get up (hard).

Build mode (F2)
- [ ] Asset browser thumbnails load for every pack; search and filters work *(Synty)*.
- [ ] Place, rotate, move, scale with the gizmo; grid snap feels right; Shift turns snapping off.
- [ ] Make a prop breakable, shoot it: pieces break along natural cracks, no hollow faces, no exploding pieces.
- [ ] Save the level, quit, load it: everything comes back, including the Mood.

**Fix:**

## 4. Physics — `./physics_demo`

*Threads: Fracture interior fill, Physics bridge leftovers, Continue
kk-engine work.* Detailed hardware checks: HW-005, HW-007, HW-014.

- [ ] Every scene button spawns its scene on the floor (nothing mid-air).
- [ ] Glass shatters into shards, brick into chunks, rubber bounces, iron dents.
- [ ] Broken pieces show solid inside faces along the cracks, from every side.
- [ ] Piles settle and stop jittering; nothing sinks through the floor.

**Fix:**

## 5. Melt — `./melt_demo`

*Thread: Continue kk-engine work.* Speed on hardware: HW-012.

- [ ] Space pours lava; ice, wax, chocolate and aluminium each melt differently and at a believable speed.
- [ ] The liquid surface looks like a liquid, not a pile of balls.
- [ ] Melt starts at the block's surface (no invisible cube around it, BUG-049).
- [ ] R resets when the liquid budget is full.

**Fix:**

## 6. Sea — `./sea_demo`

*Thread: Continue kk-engine work.* Hardware: HW-013.

- [ ] Arrow keys drive the boat; it rides the swell and doesn't capsize by itself.
- [ ] 1-5 pick what to throw; foam and wood float, iron sinks.
- [ ] C follows the boat; R resets.

**Fix:**

## 7. Jiggle — `./jiggle_demo`

*Thread: Jiggle physics and demos.*

- [ ] The jelly on the plate wobbles and settles; Space rains balls onto it.
- [ ] The jogging character's soft tissue moves naturally, not rubbery or stiff.
- [ ] Tab switches between the scenes.

**Fix:**

## 8. Audio — `./audio_demo` (headphones)

*Thread: Lua v2, audio v2, thumbnails.* HW-017 passed on 2026-09-27;
this only re-checks the ambience that came later.

- [ ] The background loop is pleasant, at a sensible volume, and loops without a click.

**Fix:**

## 9. Farm — `./farm_demo` *(Synty)*

*Thread: AI behavior and farm wildlife.*

- [ ] Sheep bolt as a flock when you run at them and calm down after.
- [ ] Cows come to look; pigs root about; horses spook; the fox keeps its distance.
- [ ] Space (bark) makes every animal in range react.
- [ ] Animals walk round fences, the barn and trees (F2 shows the navmesh).
- [ ] Teaching: Tab picks a lesson, E shows it, L makes them learn; the change is visible.
- [ ] Animals face the way they walk (no moonwalking or sliding).

**Fix:**

## 10. Procedural animation — `./procedural_demo`

*Thread: Procedural animation.*

- [ ] Spider, beetle, dog and person walk over hills and steps with feet planted (no sliding).
- [ ] Clicking the ground brings everyone to the flag.
- [ ] A click staggers the dog or person; Shift+click knocks them down and they get up.
- [ ] 1 / 2 / 3 walk, trot, gallop look like a real dog's gaits.

**Fix:**

## 11. Pet companion — `./pet_companion` *(Synty)*

*Thread: Pet companion and platoon demos.*

- [ ] Come, Sit, Stay, Fetch, Drop (1-5 or D-pad) each do what they say.
- [ ] The order wheel (Tab / LB) is easy to use with mouse and controller.
- [ ] Petting (E / Y) looks cute and reads as petting.
- [ ] The pug never blocks your path or clips through you.

**Fix:**

## 12. Platoon — `./platoon`

*Thread: Pet companion and platoon demos.*

- [ ] Selecting (click, box, groups 1-9) and ordering (right click, wheel) feel like an RTS.
- [ ] Soldiers take cover, keep formation, focus fire; the enemy fights back sensibly.
- [ ] Controller: reticle select, RB orders, LB wheel all work.

**Fix:**

## 13. Duel — `./duel`

*Thread: Fighting and goblin horde demos.*

- [ ] Jab, uppercut, knee, block/parry, dodge each read clearly and feel responsive.
- [ ] Knockdowns ragdoll and the fighter gets up without popping.
- [ ] The bot is beatable but not trivial.
- [ ] F2 / Back: a second player takes the red corner (second pad or arrow keys).

**Fix:**

## 14. Goblin horde — `./goblin_horde` *(Synty)*

*Thread: Fighting and goblin horde demos.*

- [ ] Goblins surround you, wait their turn, and break and run when morale drops.
- [ ] Slash hits two or three in front; the great swing sends everyone flying.
- [ ] Frame rate holds with a big wave.

**Fix:**

## 15. UI showcase — `./rmlui_demo`

*Threads: Xelu button prompts, Continue kk-engine work.* Full desktop
check: HW-008.

- [ ] The live input tester shows the right Xelu glyph for keys, pad buttons and sticks.
- [ ] The whole menu can be driven with a controller (focus ring visible, no mouse).

**Fix:**

## 16. Synty demo — `./synty_demo` *(Synty)*

*Thread: Physics bridge leftovers (ragdolls).* Full check: HW-009.

- [ ] Ragdolls (R, Shift+R, T, G) fall believably, joints stay in human and animal ranges.

**Fix:**

## 17. Moods and skies (every demo)

*Thread: Skies and mood for demos, Threat Interactive rendering review.*

- [ ] Each demo's sky and light fit it (Climb Race golden hour, Goblin Horde sunset, Duel night ring...).
- [ ] No banding or dithering noise in the sky or fog.
- [ ] `KKE_MOOD=night ./climb_race` (and a couple of others) changes the whole look.
- [ ] Colours look natural (AgX), not washed out or oversaturated.

**Fix:**

## 18. Starter game, tutorials and cookbook

*Threads: Docs site and starter template, Docs cookbook with examples.*

- [ ] `tools/new_game my_game`, build, run: you can walk, jump and climb.
- [ ] Follow the first tutorial page; every step works as written.
- [ ] Open https://khyretos.github.io/kk-engine/ — pages load, cookbook screenshots show.
- [ ] `./cookbook`: keys 1-8 switch between the eight cameras (first, third, orbit, top-down, isometric, side-on, fixed, cinematic); each feels usable.
- [ ] Copy two recipes from the cookbook into your game's scripts; they do what the page says.

**Fix:**

## 19. Servers, join codes and saves

*Threads: Networking v2, Server Lua and join codes, Game saves and voice
polish.* Commands are in [SERVER_HOSTING.md](SERVER_HOSTING.md).

- [ ] `./kke_server` starts, the console answers (`help`, `seen <name>`, `backup`).
- [ ] With a relay (`kke_server --roles relay`), a game server gets a join code and a friend joins by typing it.
- [ ] F1 → Network → Internet servers lists a server and Join works.
- [ ] Input replay (`KKE_NET_REPLAY=1` on the host): the joiner can run the parkour lane without rubber-banding.

**Fix:**

## 20. Mods and DLC

*Thread: DLC, mods and workshop.* Commands in [MODDING.md](MODDING.md).

- [ ] `kke_packs new` then `kke_packs check` on a test pack: messages are clear.
- [ ] A pack that overrides a file really changes it in the game; removing it brings the original back.

**Fix:**

## 21. Downloads and releases

*Threads: Downloadable builds, Local release build errors.*

- [ ] The v0.1.0-alpha download (GitHub Releases) unzips and runs on Linux (and Windows if you have it).
- [ ] A demo without packs shows its "assets not found" screen, not a crash.
- [ ] Your local GCC 16 build has zero warnings.

**Fix:**

## 22. Website — engine.kreative-kompas.com

*Thread: Presentable repo and README.*

- [ ] Front page, feature sections and screenshots look right on desktop and phone.
- [ ] The README front page on GitHub reads well and its links work.

**Fix:**

---

## Threads with nothing to look at

These threads finished work that has no look or feel to check; they can
be resolved once CI is green: *Weekly build and test health* (a routine),
*Check network access*, *Engine status audit*, *Check new share link*,
*Recheck new share link*, *Zero warnings pass*, *Roadmap and path to
market*, *Benchmarks and contributor setup*, *Non-invasive anti-cheat*
(covered by the shipping build in CI), *JSON and YAML everywhere*,
*Dependencies and licences*, *Console and platform targets* (real
hardware checks are issues #69 and #70), *Fix failing CI pipeline*,
*Polish and verify idle threads*.
