# Lesson K40: A described insert of three bindings lands inside another lambda

Situation: ENG-G07 job `g07-bset` (2026-10-13): after the light.point
binding in ScriptModule::bindAll, add a helper lambda and the light.move
and light.set bindings. The anchor (end of light.point) was unique, but the
new code was DESCRIBED line by line, not dictated as a REPLACE block.

Wrong output (all three runs): the code went into a later input.* lambda
(`'vm' is not captured`), each binding called ownLight twice and kept an
unused `slot`, and light.set wrapped each field in `lua_getfield(...) !=
LUA_TNIL` (leaving five values on the Lua stack) instead of the
fieldX(..., fallback) calls given (wrong-output.gz).

Corrected: Claude wrote the 19 lines as specified.

Rule: K25 again: for code that goes after an anchor, put the anchor AND the
new code in the REPLACE block, verbatim. Described inserts failed in every
run of this task (K31, K35, K40); dictated ones passed.

Rerun: pass = chk_g07.py bset prints ok.
