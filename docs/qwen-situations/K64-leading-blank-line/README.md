# K64: a dictated block that starts with a blank line

Job `g09b-rmlvulkanl4` (ENG-G09 follow-up, made by diffjobs.py from Claude's
diff). One block's SEARCH and REPLACE both started with an empty context
line. The Coder's answer kept the blank line in REPLACE but not in SEARCH, so
the file got two blank lines between two functions. The check (needles,
compile) could not see it; Claude removed the line in review.

Fix: diffjobs.py trims blank lines at both ends of a block, from SEARCH and
REPLACE alike.
