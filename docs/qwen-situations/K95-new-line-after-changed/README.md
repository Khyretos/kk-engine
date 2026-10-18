K95 (f10b, 2026-10-18, the third run of the K93 unit): the block changes `VkDescriptorPoolSize size{ ..., 2 * ... }` to `3 *` and
adds `m_identityLut = ...` right after it. In all three runs the Coder wrote the new line before the changed one. The check then
gave the exact SEARCH/REPLACE edit, and the repairs invented changes instead (a `si.pImageLayout` member that VkSamplerCreateInfo
does not have, a comment "Ensure separation ..."). Failed after 3 attempts each time. The same swap happened in f10 (BloomPass.cpp).
Rule: when a changed line is followed by a new line, the prompt says the new line comes right after it, not before (diffjobs).
