# Lesson K25: Insert in the right place

Situation: ENG-G04, job `g04-light` (2026-10-13): The job asked to add
lines after an anchor line (`float intensity = 1.0f;` at the end of
struct Light).

Wrong output: Asked to add lines AFTER an anchor line (`float intensity
= 1.0f;` at the end of struct Light), the Coder put them BEFORE it, or
above the anchor. The same happened in four units: g04-hdr (below
gridFor when told above), g04-light, g04-apph (above m_shadowMap), and
g04-appvp (above the `lightViewProj` line). Repair rounds did not move
them.

Corrected: Claude rewrote the prompt with the whole REPLACE block
verbatim: the anchor line first, then the new lines, in their final
order. Three units then passed on run 2; for g04-appvp Claude moved the
anchor line itself.

Rule: Never describe where new lines go; dictate the whole REPLACE block
(anchor included) in its final order, and the check tests the order.

Rerun: pass = new lines after the anchor, nothing moved. The more-*
folders hold the other three units.
