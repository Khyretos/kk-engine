K109 (CI runners, 2026-10-19): unit ci1-ci1 dictated a five-line comment block inserted between `jobs:` and the first
job's own comment in .forgejo/workflows/ci.yml, plus the jobs' new runs-on labels. The Coder put the block after the
first job's comment (inside the run of comments, not before it), and its repairs also moved `cancel-in-progress: true`
above the comment that belongs to it.
Rule: for every run of lines a block inserts between two existing lines, the prompt names both: "go between X and Y:
right after the first, right before the second" (diffjobs K109; K96 did this only above a one-line SEARCH).
wrong-output is the ci.yml the Coder left.
