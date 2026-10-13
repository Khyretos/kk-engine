# K59: a repair round put the missing comment back in the wrong place

Jobs `g08-mcrecord` and `g08-mcevery` (ENG-G08): each added a dictated function with its comment
block above it, SEARCH/REPLACE dictated whole. Attempt 1 wrote the function but dropped the comment
lines. The check said "missing `// ...`", and the repair pasted those lines where it could: inside
the function body before the dispatch (mcrecord), and below the function, above the next one
(mcevery). The check flattens the file, so the lines' place was not checked and both passed.

Corrected by Claude: both comments moved to directly above their functions. The checks now also
say which line each comment block must be right above (`commented` on every new function).
