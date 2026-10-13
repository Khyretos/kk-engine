# Lesson K26: Repair rounds duplicate code

Situation: ENG-G04, job `g04-ctx` (2026-10-13): the job asked for two
edits in struct ShadowRenderContext to add a new field while keeping the
existing one.

Wrong output: The draft replaced the `tile` field instead of adding
after it and wrote `// //`; the repair rounds then added the new field
twice and a stray `};`. In g04-hdr the repair rounds pasted copies of
five existing declarations, and in g04-apppass a repair round added `}
// Application class` at the end of the file to balance a brace it had
dropped.

Corrected: Claude reverted and ran it again as one edit with the REPLACE
block verbatim; it passed on run 2.

Rule: Checks count landmark lines (each existing declaration and closing
brace exactly once) and say which one is doubled; one edit per job
inside a struct.

Rerun: pass = one `int pointLight = -1;`, the `tile` field kept, no
stray brace.
