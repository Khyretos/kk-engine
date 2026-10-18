K105 (r1c, 2026-10-19): unit r1c-modelmodul5 passed its check with nine lines of ModelModule::render (from
`if (!m_batches.empty()) {`) indented four spaces deeper than dictated. Every check compared lines with whitespace
squeezed out, so nothing noticed; Claude put the dictated text back by hand.
Rule: each dictated REPLACE block must stand in the file exactly, indentation too, and the check hands over the one
SEARCH/REPLACE that fixes it (chk_g09 K105). wrong-output is the unit's file.
