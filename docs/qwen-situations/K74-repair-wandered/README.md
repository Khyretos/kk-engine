K74 (g19b, 2026-10-17): a job of three blocks in engine/src/ProceduralAnim.cpp that turns solveFabrik into a call of a new shared
template (the function's body moves into the template, its signature stays). Attempt 1 left out the closing brace and blank line
after the new solveFabrik. The repair rounds, given "these lines must be removed" / "must stay" errors, went looking elsewhere in
the 600-line file and rewrote an unrelated function (gaitForSpeed: `return Gait::Walk;` became `return Gait::Auto;`), and one
answer used a `// ...` placeholder. Failed after 3 attempts; Claude restored the brace and the gait line (the rest matched the
reference).
Rule: a job that moves a function's body keeps the moved code and the new call in separate jobs (one block each), and a repair
round's errors name the block they belong to, so the model doesn't search the rest of the file.
