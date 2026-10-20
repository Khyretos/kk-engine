K111 (r1g, 2026-10-19): LightingBuffer's set code (layout, pool and writes, 100+ lines) was rewritten into bind-group calls,
dictated in 12-line pieces (K88). Five pieces failed all three attempts (r1g-lightingbu1x, -2, -3, -5 and Application's
r1g-applicatio2); Claude applied them, plus the three K110 had marked.
Tried: marking every piece of a hunk that removes 30 or more lines as Claude's. On R1d to R1g's outcomes no threshold
separates them: at 30 it marks 16 of the 23 Claude-applied units and 13 the Coder passed; at 70, 9 and 8. Pieces of big
rewrites mostly pass. Rule: none new; K110 stays, and a failed unit goes to Claude after its three attempts (autohand.sh).
wrong-output holds r1g-lightingbu2's answers.
