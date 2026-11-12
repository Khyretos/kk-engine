-- sv_scores.lua: the scoreboard's truth. sv_ scripts run only where the
-- game's truth lives: on the host, or when playing alone. Players ask for
-- points; this decides, keeps the totals and tells everyone.
-- (docs/cookbook/networking.md)

local scores = {}   -- player id -> points

local function publish()
  -- To every player's machine (does nothing when playing alone)...
  if net and net.connected() then net.send("scores", scores) end
  -- ...and to the scripts on this one.
  hook.Run("ScoresChanged", scores)
end

-- The host decides: a point is only counted when it's allowed. Here, one
-- per player per half second, so a hacked client can't flood it.
local last = {}
local function addPoint(player)
  local now = kke.time()
  if last[player] and now - last[player] < 0.5 then return end
  last[player] = now
  scores[player] = (scores[player] or 0) + 1
  publish()
end

hook.Add("NetMessage", "sv_scores.point", function(name, data, from)
  if name == "point" then addPoint(from) end
end)
-- The host's own player (or you, playing alone) asks without the network.
hook.Add("WantPoint", "sv_scores.local", function(player) addPoint(player) end)
