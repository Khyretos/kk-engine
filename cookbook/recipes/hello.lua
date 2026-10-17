-- hello.lua: your first script. Variables, if, loops, functions and
-- tables, each doing something you can see. (docs/cookbook/first-steps.md)

-- A variable holds a value. `local` keeps it inside this file.
local count = 8
local size = 0.6

-- A function is a named recipe you can use again.
local function colorFor(i)
  -- if / else: even numbers orange, odd numbers blue.
  if i % 2 == 0 then
    return Vec(0.95, 0.55, 0.2)
  else
    return Vec(0.25, 0.5, 0.95)
  end
end

-- A loop runs its body once for each i from 1 to count.
for i = 1, count do
  physics.box {
    pos = Vec(-4.5 + i, 0.3, 3), -- one metre apart, in a row
    size = Vec(size, size, size),
    color = colorFor(i),
  }
end

-- A table is a list (or a dictionary). # gives a list's length.
local greetings = { "Hello", "Hallo", "Bonjour", "Hola" }
for index, word in ipairs(greetings) do
  print(index .. ": " .. word .. ", world!")
end
print("There are " .. #greetings .. " greetings and " .. count .. " boxes.")
