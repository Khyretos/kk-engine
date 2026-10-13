# Lesson K23: a dictated function body swapped for the Coder's own

Situation: NET-LOOP (tests' UDP sockets on 127.0.0.1), job `loop-use`
(2026-10-13): use the new bind address in `EnetTransport`, and add
`boundAddress()` whose six-line body the prompt gave verbatim (it returns
the IP as text, "127.0.0.1").

Wrong output (3 attempts, the test failed): the three bind lines were
right, but `boundAddress()` was the Coder's own body,
`std::to_string(a.host) + ":" + std::to_string(a.port)`, which gives
"16777343:58440"; it also dropped the null check and put the function
elsewhere. The test said what it expected, and both repair rounds left the
body as it was.

Corrected: Claude put the dictated body in (the file in the PR).

Rule: when the code is dictated, the check diffs the function against
the dictated text, not only the test result. Rerun: pass = the dictated
body on attempt 1.
