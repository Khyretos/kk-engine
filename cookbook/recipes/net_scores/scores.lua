-- scores.lua: every player's side. J asks for a point; the HUD shows the
-- scoreboard the host sends. Runs on every machine.
-- (docs/cookbook/networking.md)

input.define("scores.point", "Score a point", "J")

local hud = ui and ui.open([[
<rml><head><style>
  body { width: 100%; height: 100%; font-family: Noto Sans; color: #ffffff; pointer-events: none; }
  #board { position: absolute; right: 20dp; top: 20dp; padding: 8dp 16dp; background-color: #10131ecc;
           border-radius: 8dp; font-size: 18dp; }
</style></head>
<body><div id="board">Press J to score</div></body></rml>
]])

local function me() return net and net.playerId() or 0 end

local function show(scores)
  if not hud then return end
  local lines = {}
  for player, points in pairs(scores) do
    local who = player == me() and "You" or ("Player " .. player)
    table.insert(lines, string.format("%s: %d", who, points))
  end
  table.sort(lines)
  ui.text(hud, "board", table.concat(lines, "  ·  "))
end

hook.Add("Think", "scores.keys", function()
  if not input.pressed("scores.point") then return end
  if net and net.role() == "client" then
    net.send("point", true)          -- ask the host
  else
    hook.Run("WantPoint", me())      -- we are the host (or alone): sv_scores.lua hears this
  end
end)

-- The host's copy, on the host; the network's copy, on a client.
hook.Add("ScoresChanged", "scores.show", show)
hook.Add("NetMessage", "scores.receive", function(name, data)
  if name == "scores" and type(data) == "table" then show(data) end
end)
