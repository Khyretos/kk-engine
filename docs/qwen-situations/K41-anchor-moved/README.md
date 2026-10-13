# Lesson K41: The anchor line is moved into another function

Situation: ENG-G07 job `g07-rel` (2026-10-13): one dictated line after the
last statement of ScriptModule::releaseScript, `m_scenes.erase(... mine(s.source) ...)`;
whole REPLACE block given.

Wrong output: after the draft and two repairs, the anchor line had moved into
the scene.unload binding (where `mine` does not exist) and releaseScript kept
only the new line (wrong-output.gz). SEARCH matching ignores indentation and
scene.unload ends with a similar `m_scenes.erase(...)` line; the repairs
widened SEARCH and re-indented.

Corrected: Claude moved the line back; the next unit's build (`mine` was not
declared) showed it.

Rule: an anchor with a near twin elsewhere in the file gets a second, unique
anchor line in SEARCH (here `#endif` above it), and every unit that touches a
function builds.

Rerun: pass = chk_g07.py rel prints ok and ScriptBindings.cpp.o builds.
