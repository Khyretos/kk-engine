# 443-dangling-span

Task R1h (kk-engine), units spirv and describe. Rerun: check out 602b5e2c (the input state: the stub files and tests), run job.json (context files are listed in it) with pipeline.py, and compare with corrected-output.gz (the file at a6ce9978); wrong-output.gz is the Coder's run 1.

Pipeline::describe returns spans: the Coder (3 of 3) built local vectors and spans over them (dangling; the test read stride 25017), ignoring the prompt that said function-local statics. Corrected: the two static const lines and two span variables. Rule: when a function returns views, give the owner of the data as code and say "never a local owner".
