# Lesson K44: Calls that should be assignments, and needles that cannot tell

Situation: ENG-G07 job `g07-bpoint` (2026-10-13): light.point reads five
table fields with `d.position = ScriptVM::fieldVec3(L, 1, "pos", d.position);`
and so on, dictated in the prompt.

Wrong output: the Coder wrote the calls without `d.x = `, so every lamp got
the Desc defaults (wrong-output.gz). The needles were the call text only,
so the unit passed; unit tests cannot reach the binding. The picture check
showed no lamp light; a UBO dump showed every script light at (0,0,0).

Corrected: the Coder fixed all five lines from that finding (job
g07-bpointfix, first answer right). Claude's own check for that job broke on
shell quoting (backticks), so it failed after the right answer: checks with
backticks go in a file, not in `python3 -c`.

Rule: a needle includes the whole statement (`d.position = ...;`), never a
call that can stand alone; bindings get a run-time check (picture or log).

Rerun: pass = the g07-bpoint job writes the five assignments on run 1.
