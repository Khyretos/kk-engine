-- bat.lua: what the bat's node graph says, written as Lua. The node
-- graph compiles to Lua much like this (kke/NodeGraph.h), so each node
-- is a line you could have typed. (docs/cookbook/play-to-make.md)

hook.Add("Hit", "bat.knock_over", function(e)
  if e.by ~= "bat" then return end
  play.ragdoll(e.target, e.push)   -- node "Knock over", its push from the hit
  play.sound("wood", e.point)      -- node "Play sound", where it was hit
end)
