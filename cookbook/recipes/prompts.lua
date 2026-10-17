-- prompts.lua: show the player which button to press, as a picture of
-- that button on the device they are using right now: a key on the
-- keyboard, A on an Xbox pad, Cross on a PlayStation pad, a tap on a
-- touch screen. Pick up another device and every prompt changes by
-- itself. (docs/cookbook/input.md)

-- Your own action: G on the keyboard, the right bumper on a controller.
input.define("greet", "Wave hello", "G", "rb")

-- <prompt> is an RmlUi element the engine adds: give it an action (or a
-- raw button like "touch:hold") and a label.
local panel = ui.open([[
<rml>
<head>
  <style>
    body { font-family: Noto Sans; font-size: 22dp; color: #f2f4f8; }
    div { display: block; }
    #panel { position: absolute; left: 40dp; top: 40dp; width: 520dp; padding: 16dp 24dp;
             background-color: #10141ecc; border-radius: 12dp; }
    #panel div { margin: 6dp 0; }
    #style { font-size: 16dp; color: #9aa3b8; }
  </style>
</head>
<body>
  <div id="panel">
    <div id="style"></div>
    <div><prompt action="move" label="Move"/></div>
    <div><prompt action="jump" label="Jump"/></div>
    <div><prompt action="sprint" label="Sprint"/></div>
    <div><prompt action="fire" label="Shoot"/></div>
    <div><prompt action="greet" label="Wave hello"/></div>
    <div><prompt action="ui.accept" label="Accept"/>  <prompt action="ui.back" label="Back"/></div>
    <div id="said"></div>
  </div>
</body>
</rml>]])

local function showStyle()
  ui.text(panel, "style", "Prompts for: " .. input.style())
end
showStyle()

-- InputStyle runs when the player switches devices. <prompt> elements
-- redo themselves; text you built with input.promptText needs redoing.
hook.Add("InputStyle", "prompts.style", function(style)
  print("now showing prompts for " .. style)
  showStyle()
end)

hook.Add("Think", "prompts.greet", function()
  if input.pressed("greet") then
    -- {action} in a string becomes that button's picture.
    ui.rml(panel, "said", input.promptText("Hello! Press {greet} again to wave twice."))
  end
end)

-- The picture's file, to use in your own <img> or HUD.
print("the greet button's picture: " .. tostring(input.glyph("greet")))
