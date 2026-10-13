# Lesson K18: an initializer-list edit dropped a member, and the repairs renamed it

Situation: ENG-G05, job `g05-grid` (2026-10-13): let the shadow map hold up
to 16 tiles; one step was "add `m_baseTile(resolution)` before
`m_tile(resolution)`" in the constructor's initializer list.

Wrong output (3 attempts, all failed): attempt 1 was right everywhere else
but wrote `: m_baseTile(resolution), m_tile(resolution), ...`, dropping
`m_device(device)`. Told the reference member was uninitialised, repair 1
re-added it but also renamed `m_device.` to `device.` across the file;
repair 2 changed `createTarget`'s signature and listed every member in the
initializer, out of order. Claude's part: the spec said to place the tile
with x and y but not to delete the old `r` and `res` locals, which became
unused (an error under -Werror).

Corrected: Claude kept attempt 1's setTiles, tileRect, beginTile and gridFor,
restored the constructor line and `m_device`, and removed the two locals.

Rule: for an initializer list, dictate the whole new line. When a rewrite
leaves locals unused, say to delete them. Rerun: pass on attempt 1.
