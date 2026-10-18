K99 (f12, 2026-10-18): a block in LightingBuffer.cpp whose SEARCH starts right after `const bool many = m_manyLights && ...;`.
K96's note named that line as the one above, but the same line stands in two functions (writeEnvironment and update). The
Coder worked at the other one and pasted four clusterParams lines from update() into writeEnvironment(). Failed after 3
attempts; Claude applied the unit.
Rule: K96's neighbour note only names a line found once in the file (diffjobs).
