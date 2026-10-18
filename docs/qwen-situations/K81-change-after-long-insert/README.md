K81 (f02k, 2026-10-18): one block in engine/src/RayScene.cpp adding about 100 lines (refit, remove and their helpers) after
addBox, then, past one line of context (`void RayScene::add(...) {`), changing `if (geometry >= m_geometries.size()) return;` to
also skip removed geometry. The diff joined both in one hunk, so one block. The Coder copied the 100 lines but left the changed
line as it was; told to fix it, it searched for code that is not in the file three times. Failed after 3 attempts.
Rule: a hunk that adds a run of 20 or more lines and also changes something else is split at the context between its changes;
both pieces keep that context (diffjobs `split_long`). The changed line becomes a small job of its own.
