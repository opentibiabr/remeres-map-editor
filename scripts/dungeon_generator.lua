-- @Title: Dungeon Generator (BSP)
-- @Description: Generates a dungeon with rooms and corridors using BSP
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
    print("[Dungeon] No map open.");
    return
end

local dlg = Dialog({
    title = "Dungeon Generator",
    width = 400
})
dlg:label({
    text = "Generate a dungeon with rooms and corridors."
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
    value = 60,
    min = 20,
    max = 200
})
dlg:number({
    id = "height",
    label = "Height:",
    value = 60,
    min = 20,
    max = 200
})
dlg:newrow()
dlg:number({
    id = "minRoom",
    label = "Min Room Size:",
    value = 5,
    min = 3,
    max = 15
})
dlg:number({
    id = "maxRoom",
    label = "Max Room Size:",
    value = 12,
    min = 5,
    max = 25
})
dlg:number({
    id = "depth",
    label = "BSP Depth:",
    value = 4,
    min = 2,
    max = 8
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
    text = "mountain"
})
dlg:input({
    id = "floorBrush",
    label = "Floor Brush:",
    text = "dirt"
})
dlg:newrow()
dlg:check({
    id = "spawns",
    text = "Place monster spawns in rooms",
    selected = true
})
dlg:input({
    id = "monster",
    label = "Monster:",
    text = "Skeleton"
})
dlg:newrow()
dlg:separator()
dlg:button({
    id = "go",
    text = "Generate Dungeon",
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
local wallBrush = data.wallBrush
local floorBrush = data.floorBrush
local placeSpawns = data.spawns
local monsterName = data.monster

print(string.format("[Dungeon] Generating %dx%d dungeon...", w, h))

local result = algo.generateDungeon(w, h, {
    minRoomSize = math.floor(data.minRoom),
    maxRoomSize = math.floor(data.maxRoom),
    maxDepth = math.floor(data.depth),
    seed = seed
})

local grid = result.grid
local rooms = result.rooms

local map = app.map
local wallCount = 0
local floorCount = 0

app.transaction("Generate Dungeon", function()
    -- Paint tiles  
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

    -- Place spawns in room centers  
    if placeSpawns and #rooms > 0 then
        for i, room in ipairs(rooms) do
            local roomCX = cx + room.x + math.floor(room.width / 2)
            local roomCY = cy + room.y + math.floor(room.height / 2)
            local tile = map:getOrCreateTile(roomCX, roomCY, cz)
            if tile then
                local radius = math.min(math.floor(room.width / 2), math.floor(room.height / 2), 3)
                tile:setSpawn(radius)
                tile:setCreature(monsterName, 60, Direction.SOUTH)
                print(string.format("  Spawn in room %d at (%d,%d)", i, roomCX, roomCY))
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
print(string.format("[Dungeon] Done! Walls: %d, Floors: %d, Rooms: %d", wallCount, floorCount, #rooms))
