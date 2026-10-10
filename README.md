# patches/inbox

Checks files for the queued kk-engine patches. Each patch is already applied
(with `git am`, on top of `main` 9349996) on its own Forgejo branch, so the test
machine only needs to fetch the branch:

```sh
git fetch FORGEJO <branch> && git checkout -B <branch> FORGEJO/<branch>
```

| Checks file | Branch |
|---|---|
| `platoon-duel-checks.md` | `platoon/duel-feedback` |
| `climbing-hands-checks.md` | `climb/hands-feet-lunge` |
| `sea-demo-checks.md` | `feature/sea-demo-ships` |
| `goblin-horde-checks.md` | `goblin-horde-rework` |
| `kke-demo-checks.md` | `kke-demo-world` (rounds 1-8 (complete): `kke-demo.patch`, `kke-demo-02.patch` ... `kke-demo-08.patch`; screenshots in `kke-demo-shots/`) |
| `jiggle-body-checks.md` | `feature/jiggle-realistic-body` (on `main` fa1b7f1) |
| `sandbox-checks.md` | `sandbox/feedback-toys-walk` (`sandbox.patch`, made on `main` fa1b7f1, applied on `main` 9ea425b; screenshots in `sandbox-shots/`; extra files `sandbox-toys.scene.json`, `sandbox-wmclose.c`) |
| `pet-companion-checks.md` | `pet/companion-feedback` (`pet-companion.patch`, 2 commits on `main` 018027c; screenshots in `pet-companion-shots/`) |
| `android-femfx-off-unused-checks.md` | `fix/android-unused-femfx-off` (`android-femfx-off-unused.patch`, 1 commit on `main` bcc21ec) |
| `racing-round-checks.md` | none pushed from this inbox (`racing-round.patch` was not in this batch) |

The "Apply and build" steps in the checks files that run `git am` are already
done on these branches; skip them and start from the build.
ai-lessons/: lesson files for the ai-skills library on kireserver.
