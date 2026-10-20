# 441-missing-include

Task R1h (kk-engine), unit shader. Rerun: check out 5e407f39 (the input state: the stub files and tests), run job.json (context files are listed in it) with pipeline.py, and compare with corrected-output.gz (the file at 09f8ff96); wrong-output.gz is the Coder's run 1.

All 3 runs left out #include "kke/VulkanDevice.h" although the prompt said to include it; "invalid use of incomplete type kke::VulkanDevice" was fed back twice and not fixed; run 3 added a wrong forward declaration. Corrected: one include line. Rule: put required includes into the prompt as literal lines; tell repairs "incomplete type X: add the #include of X's header, never forward-declare it".
