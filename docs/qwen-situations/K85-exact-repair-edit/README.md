K85 (f02c, 2026-10-18): a job of two blocks in engine/include/kke/Application.h: `class BehindPass;` after `class ReflectionPass;`
and three members with a comment. Attempt 1 put the class line one line too early. Told to "move" it after `class ReflectionPass;`,
the Coder deleted it and did not put it back; told to put it back, it did, but also wrote the members' comment a second time.
Failed after 3 attempts.
Rule: when a dictated block is out of order and its first and last lines stand in the file a few lines apart, the check hands over
the one exact edit: SEARCH the lines that stand there now, REPLACE with the dictated block (chk_g09).
