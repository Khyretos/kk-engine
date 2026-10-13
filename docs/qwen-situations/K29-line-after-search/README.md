# Lesson K29: Lost the line after the SEARCH block

Situation: ENG-G04, job `g04-apppass` (2026-10-13), run 2: insert a
dictated block after the one-line anchor
`m_shadowMap->endRenderPass(cmd);`, which is followed by the `}` that
closes `if (drawScene)`.

Wrong output: Run 2 of `g04-apppass` got the verbatim block right but
dropped the `}` that closed `if (drawScene)` right after the one-line
SEARCH anchor; the repair round balanced it with `} // Application
class` at the end of the file. Calls 4 and 5 in calls.json.gz are run 2.

Corrected: Claude restored the brace and removed the stray line
(minimal).

Rule: When the anchor is followed by a closing brace, put that brace in
both SEARCH and REPLACE.

Rerun: pass = the brace stays, the file ends with `} // namespace kke`
only.
