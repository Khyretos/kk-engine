K92 (f10, 2026-10-18; also f05b-renderingp1 the same day): a paragraph added to docs/RENDERING_PRINCIPLES.md right before a heading
("### 7. Tone mapping ..."). The job anchored it on the paragraph above (K67 extends a Markdown block up to its paragraph's start),
and that paragraph ends like several others ("Measured on the RX 9070 XT ... (+0.05)"). The Coder searched for the wrong one and
put the new paragraph there, and repairs moved it between look-alikes. Failed after 3 attempts both times.
Rule: a Markdown insertion followed by a heading is anchored on the heading instead: SEARCH is the heading line, REPLACE is the
new lines and the heading (diffjobs).
