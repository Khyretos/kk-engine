# Lesson K37 (Claude): a focus regex that matches nothing shows the Coder an empty file

Situation: ENG-G07 job `g07-appb`, second run (2026-10-13). Claude built
the job's `focus` regex inside a %-formatted string, so it came out with
doubled backslashes (`m_lighting\\\\.lights`) and matched no line.

Wrong output: the excerpt was only `// ...`; all three answers said, rightly,
that the code to change was not shown (calls.json.gz). No edit was made.

Corrected: Claude fixed the regex; drive_g07.sh now stops before a unit
whose focus matches no line of its file.

Rule: before queueing a job, check that each focus regex matches its file.

Rerun: not a model mistake; the job with a working focus is the rerun.
