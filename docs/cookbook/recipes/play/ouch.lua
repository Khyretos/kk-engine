-- ouch.lua: the Lua level of play-to-make, on the same blocks as the
-- pictures and the node graph. When the bat hits a person: they fall
-- over, say "Ouch!", you get a point, and three seconds later they get
-- back up. (docs/cookbook/play-to-make.md)

hook.Add("Hit", "ouch.hit", function(e)
  if e.by ~= "bat" or e.block ~= "person" then return end
  play.ragdoll(e.target, e.push)
  play.sound("bonk", e.point)
  play.say("Ouch!")
  play.addScore(1)
  timer.Simple(3, function()
    if play.isDown(e.target) then play.standUp(e.target) end
  end)
end)

hook.Add("StoodUp", "ouch.up", function(e)
  play.say("I'm OK!")
end)
