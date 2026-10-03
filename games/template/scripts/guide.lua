-- guide.lua: the card in the corner that says what the starter game is for.
-- It is a small example of on-screen text from Lua (ui.open) and of your own
-- button (input.define). Delete this file when your game no longer needs it.

-- H on the keyboard, d-pad down on a controller: show or hide the card.
input.define("guide", "Show or hide the guide", "H", "dpad_down")

local card = ui.open([[
<rml>
<head>
  <style>
    body { font-family: Noto Sans; font-size: 17dp; color: #f2f4f8; }
    div { display: block; }
    #card { position: absolute; left: 24dp; top: 24dp; width: 470dp; padding: 14dp 20dp;
            background-color: #10141ed9; border-radius: 12dp; }
    h1 { display: block; font-size: 24dp; font-weight: bold; margin-bottom: 4dp; }
    .lead { color: #ffd27a; margin-bottom: 8dp; }
    .row { margin: 5dp 0; }
    .code { color: #9fe2ff; }
    .dim { color: #9aa3b8; font-size: 14dp; margin-top: 8dp; }
  </style>
</head>
<body>
  <div id="card">
    <h1>Starter game</h1>
    <div class="lead">The starting point for your own game. There is no goal yet: that part is yours.</div>
    <div class="row">Everything here, the floor, stairs, fence and crates, is built by
      <span class="code">scripts/game.lua</span>. Change it and save: the game updates while you play.</div>
    <div class="row">Make your own copy: <span class="code">tools/new_game my_game</span></div>
    <div class="row">Try it: walk up the stairs, <prompt action="jump" label="vault the fence"/>, push the crates.</div>
    <div class="row"><prompt action="move" label="Move"/>   <prompt action="sprint" label="Sprint"/></div>
    <div class="dim"><prompt action="guide" label="Hide this"/>  ·  Step by step: docs/tutorials</div>
  </div>
</body>
</rml>
]])

local shown = true
hook.Add("Think", "guide.toggle", function()
  if input.pressed("guide") then
    shown = not shown
    ui.show(card, shown)
  end
end)
