# Lesson K33: A dictated line loses a word, and the check doesn't say which

Situation: ENG-G07 job `g07-lbslot` (2026-10-13): insert one dictated
line, `const std::array<int, Lighting::kMaxLights> slots =
lighting.pointShadowSlots();`, above the lights loop in
LightingBuffer::update (whole REPLACE block given).

Wrong output: the draft answered twice with no edit blocks; repair 1
wrote the line without `const` (wrong-output.gz); repair 2 ran out of
tokens. The check only said "missing `<whole block>`", so the repair
could not see that one word was the problem.

Corrected: Claude added `const`. chk_g07.py now prints the nearest line
of the file next to each missing needle.

Rule: a needle error names the nearest line the model wrote, so a repair
round is about one token, not the whole block.

Rerun: pass = chk_g07.py lbslot prints ok on run 1.
