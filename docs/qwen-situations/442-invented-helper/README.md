# 442-invented-helper

Task R1h (kk-engine), unit pipeline. Rerun: check out 09f8ff96 (the input state: the stub files and tests), run job.json (context files are listed in it) with pipeline.py, and compare with corrected-output.gz (the file at 602b5e2c); wrong-output.gz is the Coder's run 1.

A 150-line move: the Coder invented VulkanGpuDevice::createPipelineLayout (in no header), redeclared class VulkanGraphicsPipeline in the .cpp, used rhi::vulkan::stagesOf (it is VulkanCommandList::stagesOf), VkDescriptorSetLayout*, members m_layout/m_pipeline that do not exist, and set the stencil state with the test off and failOp from the pass op. All 3 runs failed the build. Corrected by scripted edits (about 20 lines). Rule: a unit is one function of at most about 60 lines; split big moves; list the exact names the code may call and declare no class or function; name the old block special cases.
