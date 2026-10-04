# Lesson: moving kk-engine's home from GitHub to Forgejo (2026-10-04)

Roles: orchestrator (order of steps, switch moment), worker (CI image, workflows),
runner (build/push the image), shared (pushing from cloud sessions).

## 1. Can a cloud session reach Forgejo? Test, don't guess
```
curl -sS -o /dev/null -w '%{http_code}\n' https://git.kreative-kompas.com/api/v1/version  # 200 = host allowed
git ls-remote https://git.kreative-kompas.com/khyretos/kk-engine.git | wc -l              # read works
timeout 10 bash -c 'echo > /dev/tcp/git.kreative-kompas.com/122' || echo ssh-blocked      # SSH blocked
```
- Push needs a token: claude.ai Project settings -> Cloud environment -> gear next to
  "Default" -> env var `FORGEJO_TOKEN` (scopes write:repository, write:issue, write:package).
  Never in chat or Git. Only NEW sessions see it; the session that asked for it does not.
- `GET /api/v1/user` -> "required scope(s): [read:user]" is not a bad token. Check
  `GET /api/v1/repos/khyretos/kk-engine` -> `.permissions.push == true` instead.
- Push: `git -c http.extraHeader="Authorization: token $FORGEJO_TOKEN" push origin HEAD:main`
  (verified: a fresh session fast-forwarded main this way).
- A session without the token writes a patch (`git format-patch -1 --stdout > X.patch`) to
  /mnt/project-files; a session with the token runs `git am X.patch` and pushes.
- PRs on Forgejo: no GitHub MCP; `POST /api/v1/repos/khyretos/kk-engine/pulls`.
- Kees's secrets live in kireserver ~/Docker/_inbox (NFS on soucouyant: ~/Server-Docker/_inbox).
  Use them only on his PCs; never copy one into a cloud session (it lands in logs).

## 2. Before the switch: nothing may exist only on GitHub
1. Cloud clones are SHALLOW: every branch looked "135 behind main". `git fetch --unshallow`.
2. Per branch: `git rev-list --count origin/main..B` (0 = merged) and
   `git cherry origin/main B | grep -c '^+'`. Result: 50 branches, 43 merged, 2 orphan data
   branches (benchmark-data, gh-pages), 2 paused WIP; PR #72 obsolete (closed), #73 merged.
3. Check a PR is still needed by reading main (`grep kRingSizes ...` was empty).
4. Never delete branches or close PRs without the owner's OK; list with a recommendation.

## 3. The switch: what went wrong, in order
- Other sessions kept pushing to GitHub until the last minute. After the final pull, GitHub
  main moved 2 commits. Right after converting, compare
  `diff <(git ls-remote FORGEJO 'refs/heads/*'|sort) <(git ls-remote GITHUB 'refs/heads/*'|sort)`
  and carry every GitHub-only commit over (fast-forward push) BEFORE the push mirror's first
  sync. A push mirror overwrites GitHub; whatever was only there is gone.
- gh-pages is regenerated as one new commit each time, so it is never a fast-forward: carry it
  with `--force-with-lease`. Lasting fix: GitHub must stop pushing anything. Workflows that
  push from GitHub (docs.yml -> gh-pages, benchmarks.yml -> benchmark-data) move to
  .forgejo/workflows and push to Forgejo; the mirror carries the result to GitHub.
- A session reported "push mirror set up" when it was not: Kees saw only the
  "Add push mirror" button. Verify with the API, not with your own log:
  `GET /api/v1/repos/khyretos/kk-engine/push_mirrors` -> `last_update`, `last_error` empty.
  `last_error: Permission denied (publickey)` = deploy key missing or not write.
- Trigger a sync: `POST .../push_mirrors-sync`. Deploy key: GitHub repo -> Settings ->
  Deploy keys -> Add, "Allow write access". Forgejo shows the key next to the mirror.
- A Remote Control session's safety check blocked converting the repo with the admin token
  (hard to undo). Expected; the owner approves it or clicks it in the web UI.
- Give a tired owner ONE short instruction at a time, and say clearly what NOT to click yet.

## 4. CI on soucouyant: a toolchain image, not apt-get per run
- `runs-on: soucouyant` + `container: image: git.kreative-kompas.com/khyretos/kke-ci:1`.
  The old `ubuntu-24.04` label exists on both machines.
- `docker/ci.Dockerfile`: every package the jobs apt-installed, Node 22 (Forgejo actions run
  on Node), Android SDK + NDK r28c, MinGW + Wine. 8.7 GB unpacked.
- Job containers have no Docker daemon, so the image is built on the soucouyant host and
  pushed: `docker build -f docker/ci.Dockerfile -t .../kke-ci:N docker/` then `docker push`.
  The runner has `force_pull: false`: never re-push a tag; bump N and the `image:` lines.
- `docker login` without a credsStore saves the token in plain text in ~/.docker/config.json:
  use a token with only write:package.
- `KKE_JOBS: "8"` caps `cmake --build -j` (32 GB RAM shared with ComfyUI).
- `tools/ci/check_dependencies.py` scans Dockerfiles; new packages/hosts (`nodejs.org`,
  `procps`) need a docs/DEPENDENCIES.md row. Run it before every push.

## 5. Mistakes while building the image
- `SDKMANAGER_OPTS` is read by the sdkmanager launcher as JVM options: "Unrecognized option:
  --proxy=http". Renamed the arg to `SDK_PROXY_ARGS`. Grep a tool's launcher before naming a
  variable after the tool.
- Cloud sandbox only: start `dockerd &` yourself; builds need `--network host`, a base image
  with the proxy CA (`--build-arg BASE=...`) and sdkmanager `--proxy=http --proxy_host=127.0.0.1
  --proxy_port=PORT`. Disk hit 95% (image + 3 build trees): `docker builder prune -af`.

## 6. Test a workflow without the runner
Parse the YAML, write each `run:` step to a script, run them in order in
`docker run --network host -v WS:/ws kke-ci:test` with `bash --noprofile --norc -e -o pipefail`.
Verified in the image 2026-10-04: release-build (0 warnings, 991 tests), release linux
(package + headless demo smoke), android (both APKs, 0 warnings), windows (0 warnings,
987/989 tests under Wine; the 2 failures are real Windows test bugs, see below), docs, and
benchmarks up to the showcase stress test, which hangs on main (pre-existing).
Harness traps that are NOT CI failures: a re-configure in an old build dir warns
"CMAKE_TOOLCHAIN_FILE not used" and an unused -D override warns too; always start clean.
Windows test bugs found: test_known_packs.cpp:99 compares "a\\b/c" with "a\\b\\c" (build
the expected path with path::make_preferred or compare std::filesystem::path), and
CookedFile.ReadAssetFileReadsPlainAndCookedAndTraces keeps trace.txt open, so remove_all
fails with "Sharing violation".

## 9B capability note
A 9B model can do sections 2 and 4 with this file open, one job edit per step, running
check_dependencies.py after each. It must not run the switch (section 3): ordering across
sessions and verifying mirror state needs a stronger orchestrator or the owner.
