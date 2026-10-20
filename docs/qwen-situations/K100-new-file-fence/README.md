K100 (r1a, 2026-10-18): a new Markdown file (docs/plans/rhi.md) was asked for "in one ```cpp fence" with "Add no function,
member or helper of your own". The Coder wrote a paragraph saying it can't write a .cpp file for Markdown, then the text in a

```markdown fence; the pipeline kept all of it, and the check passed, since every dictated line was in the file.
Rule: a new file's fence names its own kind (markdown, python, yaml, json, sh) and the K72 line is for code only (diffjobs); the
check requires a new file to be exactly the dictated text (chk_g09). Rerun with both: passed on attempt 1.
