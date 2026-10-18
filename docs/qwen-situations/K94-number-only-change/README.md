K94 (f10b, 2026-10-18, the K93 unit run again): the block changes three lines by one number each (`, 2>` to `, 3>`, `i < 2` to
`i < 3`, `2 * kMaxFramesInFlight` to `3 *`). The Coder's SEARCH left out the `for` line, so it kept `i < 2`; the first repair
fixed it but the check still named the block, and the second repair added a comment line of its own ("Ensure separation ...").
Failed after 3 attempts.
Rule: every line whose only change is a number is named in the prompt with its old and new form (diffjobs, K94 note).
