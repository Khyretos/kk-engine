K108 (r1e, 2026-10-19): unit r1e-texture5 dropped Texture::createImage (about 100 lines: staging, barriers, copy, view)
and rewrote the samplers around it; r1e-texture7 rewrote the half-float sky constructors. With K106's applier fix and
K88's pieces, all three attempts still deleted too much or too little, as nine R1d units had. Claude applied both.
Rule: a unit removing more than 24 lines is Claude's from the start (diffjobs marks it "hand"; seq17 applies it as
dictated with jobs/handunit.py and runs its check), so the Coder's three attempts aren't spent on it. In R1d and R1e
that catches 2 of the 11 units Claude applied: the others removed fewer lines but rewrote blocks; they stay with
the Coder for now. wrong-output holds r1e-texture5's three answers.
