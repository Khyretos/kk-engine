# K57: an "add this function" job spent its edits on setup first

Job `g08-data` (ENG-G08): add packSkinData after packSkinBind in engine/src/GpuSkin.cpp,
the header kke/GpuSkin.h in context. The prompt described the body statement by statement
and left the SEARCH block to the model.

What came back, over three attempts: first an extra `#include "kke/JigglePhysics.h"` (already
in the header) and nothing else; then a redeclaration of the header's constants under
`// Constants`, with a wrong value (`kSkinVec4sPerBone = 1`, the header says 4); only the third
attempt added the function, and the two earlier edits stayed. The check failed on the missing
function in rounds 1-2, then on the redefinitions.

Corrected: the SEARCH block is dictated (the file's last lines) and the REPLACE block is the
whole new text (K56), with "declare nothing else: the header's constants and includes are
already visible". Rerun: python3 g08.py job.json data, apply to the input file in calls.json.gz.
