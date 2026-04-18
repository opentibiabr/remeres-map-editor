-- @Title: Cave Generator  
-- @Description: Generates a cave system using cellular automata  
-- @Author: VLL Systems  
-- @Version: 1.0.0  
if not app.hasMap() then
    print("[Cave] No map open.");
    return
end

local dlg = Dialog({
    title = "Cave Generator",
    width = 400
})
dlg:label({
    text = "Generate a cave using cellular automata."
})
dlg:separator()
dlg:number({
    id = "cx",
    label = "Start X:",
    value = 1000,
    min = 1,
    max = 65000
})
dlg:number({
    id = "cy",
    label = "Start Y:",
    value = 1000,
    min = 1,
    max = 65000
})
dlg:number({
    id = "cz",
    label = "Floor Z:",
    value = 10,
    min = 0,
    max = 15
})
dlg:newrow()
dlg:number({
    id = "width",
    label = "Width:",
    value = 50,
    min = 10,
    max = 200
})
dlg:number({
    id = "height",
    label = "Height:",
    value = 50,
    min = 10,
    max = 200
})
dlg:newrow()
dlg:number({
    id = "fill",
    label = "Fill % (0-100):",
    value = 45,
    min = 20,
    max = 80
})
dlg:number({
    id = "iterations",
    label = "Iterations:",
    value = 4,
    min = 1,
    max = 10
})
dlg:number({
    id = "seed",
    label = "Seed:",
    value = 42,
    min = 1,
    max = 99999
})
dlg:newrow()
dlg:input({
    id = "wallBrush",
    label = "Wall Brush:",
    text = "mountain"
})
dlg:input({
    id = "floorBrush",
    label = "Floor Brush:",
    text = "dirt"
})
dlg:newrow()
dlg:separator()
dlg:button({
    id = "go",
    text = "Generate Cave",
    focus = true,
    onclick = function(d)
        d:close()
    end
})
dlg:show()

local data = dlg.data
local cx = math.floor(data.cx)
local cy = math.floor(data.cy)
local cz = math.floor(data.cz)
local w = math.floor(data.width)
local h = math.floor(data.height)
local fillProb = data.fill / 100.0
local iterations = math.floor(data.iterations)
local seed = math.floor(data.seed)
local wallBrush = data.wallBrush
local floorBrush = data.floorBrush

print(string.format("[Cave] Generating %dx%d cave (fill=%.0f%%, iter=%d)...", w, h, fillProb * 100, iterations))

local grid = algo.generateCave(w, h, {
    fillProbability = fillProb,
    iterations = iterations,
    birthLimit = 4,
    deathLimit = 3,
    seed = seed
})

local map = app.map
local wallCount = 0
local floorCount = 0

app.transaction("Generate Cave", function()
    for gy = 1, #grid do
        for gx = 1, #grid[gy] do
            local x = cx + gx - 1
            local y = cy + gy - 1
            local tile = map:getOrCreateTile(x, y, cz)
            if tile then
                if grid[gy][gx] == 1 then
                    tile:applyBrush(wallBrush, false)
                    wallCount = wallCount + 1
                else
                    tile:applyBrush(floorBrush, false)
                    floorCount = floorCount + 1
                end
            end
        end
    end

    -- Borderize  
    for gy = -1, #grid + 2 do
        for gx = -1, #grid[1] + 2 do
            local tile = map:getOrCreateTile(cx + gx - 1, cy + gy - 1, cz)
            if tile then
                tile:borderize()
            end
        end
    end
end)

app.setCameraPosition(cx + math.floor(w / 2), cy + math.floor(h / 2), cz)
print(string.format("[Cave] Done! Walls: %d, Floors: %d", wallCount, floorCount))
