K98 (f11, 2026-10-18): a three-line `cpuOnly = ... ? ... : ...` statement whose first line changed; K80 trimmed its two long
continuation lines off the SEARCH, so the block ended in the middle of the statement (`... ? "attract"`). The Coder replaced the
whole statement, deleting the two lines it was never shown, and the repairs duplicated closing braces of the block above.
Failed after 3 attempts; Claude applied the unit.
Rule: K80 trims a line off a block only where a statement ends (the line before ends with ; { } or is a comment), so a
SEARCH never cuts a statement in two (diffjobs trim_long).
