-- @Title: Maze Generator  
-- @Description: Generates a maze on the map using recursive backtracking  
-- @Author: VLL Systems  
-- @Version: 1.0.0  
if not app.hasMap() then
    print("[Maze] No map open.");
    return
end

local wallBrush = Brushes.get("stone wall") -- ajuste para o nome do brush de parede do seu server  
local groundBrush = Brushes.get("grass")

local dlg = Dialog({
    title = "Maze Generator",
    width = 380
})
dlg:label({
    text = "Generate a maze on the map."
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
    value = 7,
    min = 0,
    max = 15
})
dlg:newrow()
dlg:number({
    id = "width",
    label = "Width (odd):",
    value = 31,
    min = 7,
    max = 101
})
dlg:number({
    id = "height",
    label = "Height (odd):",
    value = 31,
    min = 7,
    max = 101
})
dlg:newrow()
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
    text = "stone wall"
})
dlg:input({
    id = "floorBrush",
    label = "Floor Brush:",
    text = "grass"
})
dlg:newrow()
dlg:separator()
dlg:button({
    id = "go",
    text = "Generate",
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
local seed = math.floor(data.seed)
local wallName = data.wallBrush
local floorName = data.floorBrush

-- Force odd dimensions  
if w % 2 == 0 then
    w = w + 1
end
if h % 2 == 0 then
    h = h + 1
end

print(string.format("[Maze] Generating %dx%d maze at (%d,%d,%d)...", w, h, cx, cy, cz))

local grid = algo.generateMaze(w, h, {
    seed = seed
})

local map = app.map
local wallCount = 0
local floorCount = 0

app.transaction("Generate Maze", function()
    for gy = 1, #grid do
        for gx = 1, #grid[gy] do
            local x = cx + gx - 1
            local y = cy + gy - 1
            local tile = map:getOrCreateTile(x, y, cz)
            if tile then
                if grid[gy][gx] == 1 then
                    -- Wall  
                    tile:applyBrush(floorName, false)
                    tile:applyBrush(wallName, false)
                    wallCount = wallCount + 1
                else
                    -- Floor (path)  
                    tile:applyBrush(floorName, false)
                    floorCount = floorCount + 1
                end
            end
        end
    end

    -- Borderize  
    for gy = -1, #grid + 1 do
        for gx = -1, #grid[1] + 1 do
            local x = cx + gx - 1
            local y = cy + gy - 1
            local tile = map:getOrCreateTile(x, y, cz)
            if tile then
                tile:borderize()
            end
        end
    end
end)

app.setCameraPosition(cx + math.floor(w / 2), cy + math.floor(h / 2), cz)
print(string.format("[Maze] Done! Walls: %d, Floors: %d", wallCount, floorCount))
