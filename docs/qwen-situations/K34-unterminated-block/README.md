# Lesson K34: The answer stops after the REPLACE lines, with no closing marker

Situation: ENG-G07 job `g07-apps` (2026-10-13): replace four dictated lines
of the point-shadow pass in Application::run (whole REPLACE block given).

Wrong output: the draft and its retry were RIGHT but ended straight after
the last replacement line, without `>>>>>>> REPLACE`, so pipeline.py
found "no edit blocks" and threw them away; repair 1 the same; repair 2
applied a block that dropped the inner `for` line (wrong-output.gz).

Corrected: Claude applied run 1's answer. pipeline.py's apply_patch now
closes an unterminated last block (job g07-pipefix2, Coder, run 1).

Rule: an answer whose last block is cut after its REPLACE lines is applied
as if closed; only a block with no `=======` is lost.

Rerun: pass = the job applies on run 1 and chk_g07.py apps prints ok.
