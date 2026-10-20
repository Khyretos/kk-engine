# 444-make-unique-twice

Task R1h (kk-engine), unit ctor. Rerun: check out fcb4754d (the input state: the stub files and tests), run job.json (context files are listed in it) with pipeline.py, and compare with corrected-output.gz (the file at 9f17762e); wrong-output.gz is the Coder's run 1.

The Coder wrapped factory results that are already std::unique_ptr in std::make_unique again (borrowed(l) and createGraphicsPipeline(desc)) in 2 of 3 runs; the third invented a field primitiveRestartEnable. Corrected: two lines. Rule: say "returns a std::unique_ptr: assign or push_back it directly, no make_unique" and list the struct fields the code sets.
