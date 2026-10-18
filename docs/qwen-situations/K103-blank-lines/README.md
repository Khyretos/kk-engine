K103 (r1b, 2026-10-18): three units passed every check with a stray blank line each: one inside
VulkanCommandList.h's new Groups helper (r1b-vulkancomm1) and two in PhysicsModule::render (r1b-physicsmod units).
The needles and the added/removed line multisets all skip blank lines, so nothing noticed. Claude removed the three
by hand. Rule: a unit's blank lines added or taken out must match the dictation (diffjobs blank_delta, chk_g09).
This folder holds r1b-vulkancomm1's job and calls; wrong-output is its file.
