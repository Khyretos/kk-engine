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
| `kke-demo-checks.md` | `kke-demo-world` (rounds 1-7: `kke-demo.patch`, `kke-demo-02.patch` ... `kke-demo-07.patch`; screenshots in `kke-demo-shots/`) |
| `jiggle-body-checks.md` | `feature/jiggle-realistic-body` (on `main` fa1b7f1) |
| `racing-round-checks.md` | none pushed from this inbox (`racing-round.patch` was not in this batch) |

The "Apply and build" steps in the checks files that run `git am` are already
done on these branches; skip them and start from the build.
ai-lessons/: lesson files for the ai-skills library on kireserver.
