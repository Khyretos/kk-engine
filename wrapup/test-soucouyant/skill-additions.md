# Skill additions from the soucouyant test thread (2026-10-05)

Applied in `Services/ai/ai-skills` (Server-Docker, this commit):

* `kk-engine/SKILL.md`, Runner: work in Kees's checkout, not a clone (his rule,
  2026-10-04); push to `forgejo`, never `origin`; Kees merges PRs.
* `kk-engine/SKILL.md`, Runner extras: soucouyant env (`KKE_ASSETS_DIR`, GameCube
  ignore list, `KKE_FULL_SPEED_IN_BACKGROUND=1`, frame-cap file, 15/30/120 fps
  comparison), look at every screenshot, build without FEMFX after FEMFX merges.
* `kk-engine-vm-tests/SKILL.md`: new "Status on soucouyant" section (what ran,
  cookbook ended_early, workflow never run, phone is the primary Android device).

Not applied yet (task test-souc-06): the per-round lessons in kk-engine
`patches/inbox:ai-lessons/` (racing-cameras, platoon-duel, climbing-hands and
the later rounds) still need merging into `kk-engine-games` / `kk-engine-showcase`.
