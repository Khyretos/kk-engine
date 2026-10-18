K79 (f02k, 2026-10-18): one block in shaders/reflect.glsl adding `bool leftScreen = false;` after `float prev = 0.0, hitT = -1.0;`.
Attempt 1 left the new line out; attempt 2 put it one line too early (before `float prev`). The check then named the first line
out of order, which was the old `float prev` line, and said "move" it; the Coder deleted `float prev` and did not put it back.
Failed after 3 attempts; Claude put the line back.
Rule: when the out-of-order line is an old one, the check names the first new line of the block (diffjobs `added`) instead,
so a repair moves the line the job added and never touches the file's own lines.
