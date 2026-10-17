K80 (f02k, 2026-10-18): one block in engine/CMakeLists.txt adding `ray_reflect.comp ray_reflect_ms.comp` to the shader list.
The dictated SEARCH held seven lines, three of them about 150 characters long. Twice the Coder typed `effect_spark.frag` for
`effect_smoke.frag` in the last long line, so its SEARCH was not in the file; on attempt 3 it joined two list lines into one and
dropped the new shaders. Failed after 3 attempts; with the rule the rerun passed on attempt 1.
Rule: while a block holds an unchanged line over 100 characters, the job builder drops context lines from the block's ends as
long as what is left is found exactly once in the file (diffjobs `trim_long`): here the SEARCH is the one changed line.
