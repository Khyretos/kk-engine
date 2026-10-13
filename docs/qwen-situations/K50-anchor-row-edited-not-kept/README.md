# Lesson K50: an "anchor plus new row" insert became an edit of the anchor

Situation: BUG-090 job `b090-mrow` (2026-10-13): the metrics table of
docs/qwen-log/README.md gets a new row after the ENG-G07 row. The prompt
dictated the SEARCH block (the ENG-G07 row) and the REPLACE block (that row,
then the new one), like every other docs-row job in this thread.

Wrong output: the Coder replaced only the first cell of the ENG-G07 row with
`ENG-B090/B091 mrow` (the job's name) and kept the rest of the old row; the
new row was gone (wrong-output.gz). Both repair rounds returned no edit
blocks.

Corrected: Claude restored the file and applied the dictated block.

Rule: an anchor row longer than about 500 characters is too long to copy;
anchor on a short unique prefix line instead, or let a script append rows.

Rerun: pass = b090-mrow writes both rows on run 1.
