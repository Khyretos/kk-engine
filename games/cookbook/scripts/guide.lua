-- guide.lua: the card that says what the cookbook game is and what the
-- thing next to you shows. It changes as you walk from one part of the
-- playground to the next (player.position), and always lists the camera
-- buttons. H or the d-pad down hides it.

input.define("guide", "Show or hide the guide", "H", "dpad_down")

-- Each place: where it is, how close counts as "here", and what it shows.
local places = {
  {
    at = Vec(3, 0, 1), radius = 4,
    title = "Animation on a real skeleton",
    text = "The orange mannequin plays a plain idle clip, and code bends it on top:"
      .. "<br/>- its right hand reaches for the floating yellow ball (arm IK: the elbow and shoulder stay human)"
      .. "<br/>- its head turns to follow you (look-at)"
      .. "<br/>- one foot stands on the blue step, flat on top of it (foot placement)"
      .. "<br/>Cookbook chapter: Animation.",
  },
  {
    at = Vec(-11, 0, 9), radius = 6,
    title = "Blend space",
    text = "The walking mannequin speeds up and slows down. One number, its speed, blends the idle,"
      .. " walk and jog clips so the feet stay in step at every speed.<br/>Cookbook chapter: Animation.",
  },
  {
    at = Vec(10, 0, 0), radius = 5,
    title = "Cameras",
    text = "The pillars are something for each camera to look past. Try them all: the third person"
      .. " camera slides in instead of going through a pillar.<br/>Cookbook chapter: Cameras.",
  },
  {
    at = Vec(-6, 0, -4), radius = 5,
    title = "Moving: stairs",
    text = "The character climbs steps up to 25 cm by itself, no jump needed.<br/>Cookbook chapter: Moving.",
  },
  {
    at = Vec(4, 0, -5.5), radius = 4.5,
    title = "Moving: vault and climb",
    text = "Jump at the fence to vault it, and at the tall block to climb on top.<br/>Cookbook chapter: Moving.",
  },
}

local intro = {
  title = "The cookbook, live",
  text = "Every recipe in the docs' cookbook (docs/cookbook) running in one playground. The docs quote this"
    .. " game's code, and the build runs it, so the recipes can't quietly stop working."
    .. "<br/>Walk up to the mannequins, the pillars or the stairs: this card says what each one shows.",
}

local card = ui.open([[
<rml>
<head>
  <style>
    body { font-family: Noto Sans; font-size: 17dp; color: #f2f4f8; }
    div { display: block; }
    #card { position: absolute; left: 24dp; top: 24dp; width: 500dp; padding: 14dp 20dp;
            background-color: #10141ed9; border-radius: 12dp; }
    #title { font-size: 23dp; font-weight: bold; color: #ffd27a; margin-bottom: 6dp; }
    #text { margin-bottom: 10dp; }
    .row { margin: 4dp 0; }
    .dim { color: #9aa3b8; font-size: 14dp; margin-top: 6dp; }
  </style>
</head>
<body>
  <div id="card">
    <div id="title"></div>
    <div id="text"></div>
    <div class="row">Camera: <span id="camera"></span></div>
    <div class="row"><prompt action="camera.next" label="Next camera"/>  <prompt action="view.tour" label="Camera path"/>  <prompt action="view.shake" label="Shake"/></div>
    <div class="dim">Keys 1 to 8 pick a camera  ·  <prompt action="guide" label="Hide this"/></div>
  </div>
</body>
</rml>
]])

local shown, current, camera = true, nil, nil

local function nearest()
  local p = player.position()
  for _, place in ipairs(places) do
    local dx, dz = p.x - place.at.x, p.z - place.at.z
    if dx * dx + dz * dz < place.radius * place.radius then return place end
  end
  return intro
end

hook.Add("Think", "guide.update", function()
  if input.pressed("guide") then
    shown = not shown
    ui.show(card, shown)
  end
  if not shown then return end
  local place = nearest()
  if place ~= current then
    current = place
    ui.text(card, "title", place.title)
    ui.rml(card, "text", place.text)
  end
  local mode = view.mode()
  if mode ~= camera then
    camera = mode
    ui.text(card, "camera", mode)
  end
end)
