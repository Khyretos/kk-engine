-- terrain.lua: hills from noise. Value noise gives smooth random numbers
-- over the ground; adding a few layers of it at different sizes (fractal
-- noise) looks like terrain. Each column's height and colour come from
-- it. N makes a new landscape. (docs/cookbook/algorithms.md)

local N = 20                    -- columns across and deep
local STEP = 0.8                -- metres per column
local ORIGIN = Vec(2, 0, 4)
local columns = {}
local seed = 1

-- A fixed random number for each whole-number grid point (a hash).
local function lattice(ix, iz)
  local h = (ix * 374761393 + iz * 668265263 + seed * 1442695041) % 2147483647
  h = (h * h * 15731 + 789221) % 2147483647
  return (h % 10000) / 10000     -- 0 .. 1
end

local function smooth(t) return t * t * (3 - 2 * t) end   -- eases in and out

-- Value noise: blend the four grid points around (x, z).
local function noise(x, z)
  local ix, iz = math.floor(x), math.floor(z)
  local fx, fz = smooth(x - ix), smooth(z - iz)
  local a, b = lattice(ix, iz), lattice(ix + 1, iz)
  local c, d = lattice(ix, iz + 1), lattice(ix + 1, iz + 1)
  local top = a + (b - a) * fx
  local bottom = c + (d - c) * fx
  return top + (bottom - top) * fz
end

-- Fractal noise: big gentle hills plus smaller and smaller bumps.
local function fbm(x, z)
  local sum, amp, freq, total = 0, 1, 1, 0
  for _ = 1, 4 do
    sum = sum + noise(x * freq, z * freq) * amp
    total = total + amp
    amp, freq = amp * 0.5, freq * 2
  end
  return sum / total              -- 0 .. 1
end

local function build()
  for _, id in ipairs(columns) do physics.remove(id) end
  columns = {}
  for x = 0, N - 1 do
    for z = 0, N - 1 do
      local h = fbm(x * 0.15, z * 0.15)
      local height = 0.2 + h * h * 4                      -- squared: flatter valleys, sharper peaks
      local color
      if h < 0.35 then color = Vec(0.2, 0.45, 0.8)         -- water
      elseif h < 0.55 then color = Vec(0.35, 0.65, 0.3)    -- grass
      elseif h < 0.7 then color = Vec(0.5, 0.45, 0.35)     -- rock
      else color = Vec(0.95, 0.95, 0.97) end               -- snow
      table.insert(columns, physics.box { pos = ORIGIN + Vec(x * STEP, height / 2, z * STEP),
                                          size = Vec(STEP, height, STEP), color = color, static = true })
    end
  end
end

build()
input.define("terrain.new", "New terrain", "N")
hook.Add("Think", "terrain.keys", function()
  if input.pressed("terrain.new") then seed = seed + 1; build() end
end)
