K110 (r1f, 2026-10-19): four R1f units failed all three attempts and were applied by Claude: r1f-mesh1x (Vertex's
attribute function, 30 lines rewritten as 8), r1f-pipeline3x and r1f-pipeline4x (Pipeline's vertex input and push
constants rewritten), r1f-rmlvulkanr3 (RmlUi's stencil and vertex layout). Across R1d, R1e and R1f Claude applied 15
units; K108's mark (more than 24 removed lines) caught 2 of them.
Rule: a unit removing 16 or more lines is Claude's from the start (diffjobs). On R1d to R1f's outcomes that marks 6 of the
15 and only one unit the Coder passed; shorter rewrites stay with the Coder. wrong-output holds r1f-mesh1x's answers.
