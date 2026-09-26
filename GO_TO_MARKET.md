# GO_TO_MARKET.md — bringing Kreative Kompas Engine to people, for free

**The constraint:** no money to spend. Everything below uses free services
plus what Claude can do inside this repo and project (write code, docs,
posts, videos from headless captures, grant applications, pitch decks).
Things only Kees can do (own accounts, press "post", sign forms, talk to
people) are marked **Kees**.

**The rule:** we don't launch until the engine is solid. The launch gate
is concrete (see "Launch gate"), so "solid" isn't a feeling. Everything
before the gate is cheap groundwork that pays off on launch day.

Task tracking lives in GitHub issues, grouped by milestone label:
`M1: prototype engine` → `M2: public alpha` → `M3: platform`.

---

## 1. What we're selling (positioning)

One line: **an open, physics-first game engine that runs on a potato.**

What no free engine gives you in one package today:

| Differentiator | Evidence we already have |
|---|---|
| Real deformable and breaking physics (AMD FEMFX: plastic dents, Voronoi fracture, melting, liquids, buoyancy) | physics_demo, melt_demo, sea_demo, kke_demo breaking yard |
| Runs on a 1-core, 2 GB machine with no GPU (software Vulkan) | Stress test + physics benchmark numbers (PERFORMANCE_NOTES.md, audit 2026-09-26) |
| Garry's Mod-style Lua scripting with hot reload | `ScriptVM` / `ScriptModule` (SCRIPTING.md) |
| Accessibility built in (sound visualiser, captions, left-handed mirror, any controller incl. HOSAS and Steam Controller gyro) | AUDIO.md, INPUT.md |
| Lego modules: a game is a list of `addModule<>()` calls | README "Architecture" |

**Who it's for, in order:**
1. Hobby and indie devs who want physics toys, sandbox and destruction games
   (the Garry's Mod / Teardown / BeamNG crowd) and can't pay for or don't
   want Unreal.
2. Players with old or cheap hardware, and the devs who want to reach them.
3. Engine hobbyists who read Vulkan/C++ code for fun (they bring stars and
   contributors, not revenue).

**Honest comparison** (have this ready, people will ask): Godot is the
general-purpose open engine and far more mature; Bevy is Rust/ECS; O3DE
is heavy; Roblox and s&box are closed platforms. KKE doesn't compete on
breadth. It competes on destruction physics, low-end performance and
moddability.

### The AI-coded question

The README already says openly that Claude writes the code. Keep it that
way: honesty is the only position that survives. Some communities
(r/gamedev, Hacker News) are hostile to "AI slop", so every launch post
leads with **what it does, shown in video**, and states the AI part
plainly with what makes it trustworthy: every slice built and run before
being called done, 170+ unit tests, CI, validation layers, zero-warnings
policy, BUGS.md with root causes. The angle "one person + Claude built
a physics engine that runs on a potato" is itself a story people share.
Never hide it; hiding it and being found out kills the project.

---

## 2. Before launch (now, while the engine is being finished)

Cheap things that compound. None of them need the engine to be done.

| # | Step | Who | Cost |
|---|---|---|---|
| 2.1 | Ko-fi link in the repo (`.github/FUNDING.yml` → GitHub's "Sponsor" button) and a README badge | Claude (done) | free |
| 2.2 | Licence (#13): MIT, chosen by Kees 2026-09-26 | Claude (done) | free |
| 2.3 | Claim the name everywhere so nobody else takes it: itch.io, YouTube, Bluesky, Mastodon (mastodon.gamedev.place), Reddit, a Discord server | **Kees** (accounts must be yours) | free |
| 2.4 | Also enable GitHub Sponsors (0% fees, needs a Stripe/bank payout profile) and put it next to Ko-fi in FUNDING.yml | **Kees** signs up, Claude adds the line | free |
| 2.5 | Start a devlog: one short post per month (what landed, one GIF). Claude drafts from `git log` + BUGS.md + headless captures; Kees posts it | Claude drafts, **Kees** posts | free |
| 2.6 | Build a GIF/screenshot library: every time a visual feature lands, capture it headlessly (Xvfb + ffmpeg) and keep the best in `docs/media/` | Claude | free |
| 2.7 | Record the good footage on a real GPU (lavapipe renders correctly but slowly; trailers need 60 fps). OBS is free | **Kees** records, Claude writes the shot list and edits notes | free |
| 2.8 | Repo is public (it always was; confirmed 2026-09-26). Stars and feedback can accumulate before launch, and the devlog links to it. Rule: nothing private (asset share hash, paid packs, keys) ever goes in a commit | Claude (done) | free |

## 3. Launch gate — when we go

We launch when **all of these are true**, not before:

1. Every `M1: prototype engine` issue is closed: networking v1, split
   screen, level save/load, Lua v2, audio v2, FEMFX↔Jolt bridge, the
   in-progress threads (jiggle, fracture fill, zero warnings, flicker)
   and the finished showcase.
2. Every `M2: public alpha` issue is closed: licence, downloadable
   builds, starter template + tutorial, docs site, contributor setup,
   BENCHMARKS.md.
3. One person who has never seen KKE goes from download to their own
   running game using only the docs (#15).
4. The showcase runs at playable fps on the 1-core floor and on at least
   one real GPU from HARDWARE_TESTS.md.

Version at launch: `v0.1.0-alpha`. Calling it alpha sets expectations and
buys goodwill for rough edges.

## 4. Launch week

All free. Claude writes every post, tailored per community (rules and
tone differ a lot); **Kees** posts from Kees's own accounts and answers comments
(answering in the first hours is what makes a post take off).

| Day | Where | What |
|---|---|---|
| Mon | YouTube + itch.io | 60-90 s trailer (destruction first, then "runs on 1 core", then Lua, then "free and open"). itch.io page with kke_demo as a free download and a "name your own price" option |
| Tue | Reddit: r/gamedev (follow its self-promotion rules), r/cpp, r/vulkan, r/opensource | Tech-first posts with GIFs; r/cpp and r/vulkan get the architecture story |
| Wed | Hacker News "Show HN" | Short, honest, AI disclosure up front, link to repo + docs |
| Thu | Bluesky, Mastodon (#gamedev, #indiedev, #screenshotsaturday), gamedev.net forums, IndieDB | GIF threads |
| Fri | Awesome lists | PRs to awesome-gamedev-style lists and engine lists (free, permanent backlinks) |
| Sat | #ScreenshotSaturday | Best GIF of the week |

After launch week: one devlog per month, #ScreenshotSaturday weekly
when there's something new, answer every issue and Discussion.

## 5. After launch — growing a community for free

- **Host a game jam on itch.io** (free to host): "Break Something Jam",
  one weekend, KKE only. Jams produce sample games, bug reports and
  tutorials by other people. Claude can prepare the jam page, starter
  kit and rules.
- **Sample games:** small, complete, open-source games made in Lua on KKE
  (Claude builds them; each doubles as a tutorial and a devlog post).
- **Good first issues** labelled for contributors; CONTRIBUTING.md (#17).
- **Discord** with channels for help, showcase and devlog; GitHub
  Discussions for searchable Q&A.
- **Marketplace (#19)** with the free GitHub-backed index: every game
  someone publishes is free marketing.

## 6. Funding ladder

Realistic order, from easiest to hardest. Investors come last, not first,
and that's normal for engines: they fund traction, not plans.

| Step | Source | What it takes | Notes |
|---|---|---|---|
| 1 | **Ko-fi** (live) + **GitHub Sponsors** | Being visible | Ko-fi takes 0% on donations; Sponsors 0% for personal accounts. Put perks on tiers: name in credits, early devlog, vote on the next feature |
| 2 | **itch.io "name your price"** on the demo and later on sample games | Launch | Free to list; itch's cut is set by the creator (can be 0%) |
| 3 | **Open Collective** (optional) | A few regular backers | Transparent budget, good if others start contributing |
| 4 | **Grants for open-source tools** | Public repo, licence, traction, a clear plan (this file + ROADMAP.md) | Candidates to check when we're ready (terms change, verify at the time): Epic MegaGrants (has funded open-source tools such as Blender and Godot), GitHub Accelerator / GitHub Secure Open Source fund (when open), NLnet / NGI Zero (EU, needs an internet/open-standards angle; the networking stack could qualify), Dutch Stimuleringsfonds Creatieve Industrie game schemes (if Kees is NL-based). Claude writes the applications; **Kees** signs and submits |
| 5 | **Services** | Launch + a few users | Paid support, porting a game to KKE, custom modules. Only when it doesn't slow the engine down |
| 6 | **Early investor / publisher** | Traction numbers | What they'll ask: stars and growth, monthly active devs, games shipped, community size, revenue from steps 1-5, and a path to money (marketplace cut, pro services, hosted servers). Claude prepares a one-page pitch and a short deck from real numbers when there are numbers to show |

The marketplace (#19) is the long-term revenue idea: free engine, free
games, a small cut only on paid games once it exists. It needs players
and creators first, which is what steps 1-5 build.

## 7. What Claude does vs what Kees does

| Claude | Kees |
|---|---|
| Engine code, tests, docs, docs site, release workflow | Decide licence, go public, launch date |
| Devlog drafts, launch posts per community, trailer shot list, captions | Create accounts, post, reply to comments in the first hours |
| Headless screenshots/GIFs, benchmark tables | Record 60 fps footage on a real GPU |
| Grant applications, pitch one-pager/deck, jam page | Sign, submit, talk to people and investors |
| Track metrics monthly (stars, downloads, Ko-fi, issues) in this file | Say what feels right and what doesn't |

## 8. Metrics to watch (monthly, from launch)

GitHub stars and clones, release downloads, itch.io views/downloads,
Ko-fi and Sponsors supporters, Discord members, issues/PRs from outside,
games published with KKE. Written into a table here each month, so a grant
or investor pitch has real numbers.

---

_Status: plan written 2026-09-26. Pre-launch steps 2.1 (Ko-fi) and 2.2 (MIT licence) done; everything else
waits on the milestones above._
