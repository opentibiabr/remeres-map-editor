-- @Title: Forest Generator  
-- @Description: Generates a forest using Poisson Disk Sampling for natural tree placement  
-- @Author: VLL Systems  
-- @Version: 1.0.0  
if not app.hasMap() then
    print("[Forest] No map open.");
    return
end

local dlg = Dialog({
    title = "Forest Generator",
    width = 400
})
dlg:label({
    text = "Generate a forest with naturally spaced trees."
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
    label = "Width:",
    value = 30,
    min = 5,
    max = 200
})
dlg:number({
    id = "height",
    label = "Height:",
    value = 30,
    min = 5,
    max = 200
})
dlg:newrow()
dlg:number({
    id = "spacing",
    label = "Tree Spacing:",
    value = 3,
    min = 2,
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
    id = "groundBrush",
    label = "Ground Brush:",
    text = "grass"
})
dlg:newrow()
dlg:check({
    id = "organic",
    text = "Organic shape (noise edge)",
    selected = true
})
dlg:check({
    id = "flowers",
    text = "Add flowers between trees",
    selected = true
})
dlg:newrow()
dlg:separator()
dlg:button({
    id = "go",
    text = "Generate Forest",
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
local spacing = math.floor(data.spacing)
local seed = math.floor(data.seed)
local groundBrush = data.groundBrush
local useOrganic = data.organic
local useFlowers = data.flowers

-- Tree item IDs (adjust for your server)  
local TREES = {2700, 2701, 2702, 2703, 2704, 2706, 2707, 2708, 2709}
local FLOWERS = {2725, 2726, 2727, 2728}
local MUSHROOMS = {2736, 2737, 2738, 2739}

print(string.format("[Forest] Generating %dx%d forest at (%d,%d,%d)...", w, h, cx, cy, cz))

-- Generate tree positions using Poisson Disk Sampling  
local treePositions = geo.poissonDiskSampling(cx, cy, cx + w, cy + h, spacing, {
    seed = seed
})

print(string.format("[Forest] Poisson disk generated %d tree positions (spacing=%d)", #treePositions, spacing))

-- Helper: check if point is inside organic shape  
local function isInsideForest(x, y)
    if not useOrganic then
        return true
    end
    local centerX = cx + w / 2
    local centerY = cy + h / 2
    local dx = (x - centerX) / (w / 2)
    local dy = (y - centerY) / (h / 2)
    local dist = math.sqrt(dx * dx + dy * dy)
    local noiseVal = noise.simplex(x * 1.0, y * 1.0, seed, 0.08)
    local threshold = 0.85 + noiseVal * 0.25
    return dist <= threshold
end

local map = app.map
local treeCount = 0
local flowerCount = 0
local groundCount = 0

math.randomseed(seed)

app.transaction("Generate Forest", function()
    -- Pass 1: Paint ground  
    for dy = 0, h do
        for dx = 0, w do
            local x = cx + dx
            local y = cy + dy
            if isInsideForest(x, y) then
                local tile = map:getOrCreateTile(x, y, cz)
                if tile then
                    tile:applyBrush(groundBrush, false)
                    groundCount = groundCount + 1
                end
            end
        end
    end

    -- Pass 2: Place trees  
    for _, pos in ipairs(treePositions) do
        local x = math.floor(pos.x)
        local y = math.floor(pos.y)
        if isInsideForest(x, y) then
            local tile = map:getOrCreateTile(x, y, cz)
            if tile and tile.hasGround then
                local treeId = TREES[math.random(1, #TREES)]
                tile:addItem(treeId)
                treeCount = treeCount + 1
            end
        end
    end

    -- Pass 3: Scatter flowers/mushrooms between trees  
    if useFlowers then
        local decoPositions = geo.randomScatter(cx + 1, cy + 1, cx + w - 1, cy + h - 1, math.floor(w * h * 0.08), {
            seed = seed + 200,
            minDistance = 1
        })

        for _, pos in ipairs(decoPositions) do
            local x = math.floor(pos.x)
            local y = math.floor(pos.y)
            if isInsideForest(x, y) then
                local tile = map:getTile(x, y, cz)
                if tile and tile.hasGround and tile.itemCount < 2 then
                    local decoList = math.random() > 0.7 and MUSHROOMS or FLOWERS
                    local decoId = decoList[math.random(1, #decoList)]
                    tile:addItem(decoId)
                    flowerCount = flowerCount + 1
                end
            end
        end
    end

    -- Pass 4: Borderize  
    for dy = -2, h + 2 do
        for dx = -2, w + 2 do
            local tile = map:getOrCreateTile(cx + dx, cy + dy, cz)
            if tile then
                tile:borderize()
            end
        end
    end
end)

app.setCameraPosition(cx + math.floor(w / 2), cy + math.floor(h / 2), cz)
print(string.format("[Forest] Done! Ground: %d, Trees: %d, Flowers: %d", groundCount, treeCount, flowerCount))
