K91 (f07, 2026-10-18): a new file, shaders/shadow_rays.glsl. The job builder knew .frag, .vert and .comp as GLSL but not .glsl, so the
prompt asked for the shader "in one ```cpp fence". The Coder refused ("I cannot write C++ code for a GLSL shader file inside a cpp
fence"), and the repair rounds, patching a file that did not exist, could not see it either. Failed after 3 attempts. (Four earlier
.glsl files had gone through the same wrong fence without complaint.)
Rule: .glsl files are GLSL to the job builder too (diffjobs KIND), so a new one is asked for in a ```glsl fence.
