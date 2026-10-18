K97 (f10b, 2026-10-18): one new line, `m_renderer->setColorLut(...)`, added above a one-line SEARCH (`const SkyEnvironment* ...`;
K80 trimmed the long `setPostParams(...)` line above it). The Coder put the new line one too high, above `setPostParams`; the
repairs then moved `setPostParams` down past three lines instead. The check counts lines (K65), so a moved line was invisible
and the unit passed on attempt 3. Claude found it comparing the drafts with the reference and moved it back.
Rule: the check also requires the lines that stay to keep their order, and names a moved line and the line it must follow
(chk_g09). K96's neighbour note in the prompt covers the cause.
