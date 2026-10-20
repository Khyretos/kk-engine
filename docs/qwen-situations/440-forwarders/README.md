# 440-forwarders

Task R1h (kk-engine), unit state. Rerun: check out e5a66644 (the input state: the stub files and tests), run job.json (context files are listed in it) with pipeline.py, and compare with corrected-output.gz (the file at e236b504); wrong-output.gz is the Coder's run 2.

The Coder kept the old anonymous-namespace vk_* helpers and wrote a one-line forwarder for every function the header declares (3 of 3 runs, all passing the tests). The prompt said "the anonymous-namespace vk(...) overloads, renamed". Corrected: each declared function defined directly, no helper, no forwarder (scripted rename). Rule: when a job turns private helpers into declared public functions, say "define each declared function directly; no anonymous helper, no forwarder".
