-- @Title: Island Generator
-- @Description: Creates a small island with grass surrounded by water, a sword, and a Troll spawn. Demonstrates tile manipulation, geometry, noise, dialogs, and creature placement.
-- @Author: VLL Systems
-- @Version: 1.0.0
-- ============================================================================
-- CONFIGURATION: Item IDs (adjust these for your OT server version)
-- These are common Tibia item IDs. Change them if your server uses
-- different IDs or a different client version.
-- ============================================================================
local GROUND_GRASS = 4526 -- Green grass ground tile
local GROUND_SAND = 231 -- Sand/beach ground tile
local GROUND_WATER = 4615 -- Water ground tile
local ITEM_SWORD = 2376 -- Sword (common Tibia sword)
local ITEM_TREE = 2700 -- Small tree / palm
local ITEM_FLOWER = 2725 -- Flower decoration
local ITEM_CAMPFIRE = 1423 -- Campfire
local ITEM_SIGN = 2562 -- Sign post

local MONSTER_NAME = "Troll"
local SPAWN_TIME = 60 -- seconds
local SPAWN_RADIUS = 3

-- ============================================================================
-- PRE-CHECK: Make sure a map is open
-- ============================================================================

if not app.hasMap() then
    print("[Island Generator] No map is open. Please open or create a map first.")
    return
end

-- ============================================================================
-- CONFIGURATION DIALOG
-- ============================================================================

local dlg = Dialog({
    title = "Island Generator",
    width = 380
})

dlg:label({
    text = "Generate a small island with grass, water, items, and creatures."
})
dlg:separator()

-- Position settings
dlg:label({
    text = "Island Center Position:"
})
dlg:newrow()
dlg:number({
    id = "cx",
    label = "X:",
    value = 1000,
    min = 1,
    max = 65000
})
dlg:number({
    id = "cy",
    label = "Y:",
    value = 1000,
    min = 1,
    max = 65000
})
dlg:number({
    id = "cz",
    label = "Z:",
    value = 7,
    min = 0,
    max = 15
})
dlg:newrow()

-- Island size
dlg:number({
    id = "radius",
    label = "Island Radius:",
    value = 8,
    min = 3,
    max = 50
})
dlg:newrow()

-- Organic shape
dlg:check({
    id = "organic",
    text = "Organic shape (noise distortion)",
    selected = true
})
dlg:newrow()
dlg:number({
    id = "seed",
    label = "Noise Seed:",
    value = 42,
    min = 1,
    max = 99999
})
dlg:newrow()

-- Beach ring
dlg:check({
    id = "beach",
    text = "Add sand beach ring",
    selected = true
})
dlg:newrow()

-- Decorations
dlg:check({
    id = "decorations",
    text = "Place decorative items (trees, flowers)",
    selected = true
})
dlg:newrow()

-- Creature
dlg:input({
    id = "monster",
    label = "Monster:",
    text = MONSTER_NAME
})
dlg:number({
    id = "spawnTime",
    label = "Spawn Time (s):",
    value = SPAWN_TIME,
    min = 1,
    max = 3600
})
dlg:newrow()

-- Item IDs (advanced)
dlg:separator()
dlg:label({
    text = "Item IDs (adjust for your server version):"
})
dlg:newrow()
dlg:number({
    id = "grassId",
    label = "Grass:",
    value = GROUND_GRASS,
    min = 1,
    max = 65535
})
dlg:number({
    id = "sandId",
    label = "Sand:",
    value = GROUND_SAND,
    min = 1,
    max = 65535
})
dlg:number({
    id = "waterId",
    label = "Water:",
    value = GROUND_WATER,
    min = 1,
    max = 65535
})
dlg:newrow()
dlg:number({
    id = "swordId",
    label = "Sword:",
    value = ITEM_SWORD,
    min = 1,
    max = 65535
})
dlg:newrow()

-- Buttons
dlg:separator()
dlg:button({
    id = "generate",
    text = "Generate Island",
    focus = true,
    onclick = function(dialog)
        dialog:close()
    end
})

dlg:show()

-- ============================================================================
-- READ DIALOG VALUES
-- ============================================================================

local data = dlg.data

local cx = math.floor(data.cx)
local cy = math.floor(data.cy)
local cz = math.floor(data.cz)
local radius = math.floor(data.radius)
local useOrganic = data.organic
local useSeed = math.floor(data.seed)
local useBeach = data.beach
local useDecos = data.decorations
local monsterName = data.monster
local spawnTime = math.floor(data.spawnTime)
local grassId = math.floor(data.grassId)
local sandId = math.floor(data.sandId)
local waterId = math.floor(data.waterId)
local swordId = math.floor(data.swordId)

-- ============================================================================
-- HELPER: Check if a point is inside the island shape
-- ============================================================================

-- For organic shapes, we distort the radius using simplex noise
local function isInsideIsland(x, y, r)
    local dx = x - cx
    local dy = y - cy
    local dist = math.sqrt(dx * dx + dy * dy)

    if useOrganic then
        -- Use noise to distort the boundary
        local noiseVal = noise.simplex(x * 1.0, y * 1.0, useSeed, 0.12)
        -- noiseVal is in [-1, 1], scale it to distort radius by up to 30%
        local distortedR = r + noiseVal * r * 0.3
        return dist <= distortedR
    else
        return dist <= r
    end
end

-- ============================================================================
-- GENERATE THE ISLAND
-- ============================================================================

print("[Island Generator] Starting generation...")
print(string.format("Center: (%d, %d, %d), Radius: %d", cx, cy, cz, radius))

local map = app.map
local grassCount = 0
local sandCount = 0
local waterCount = 0
local decoCount = 0

app.transaction("Generate Island", function()

    -- We need to cover the full area: water ring extends radius + 3
    local outerRadius = radius + 3
    local allTiles = {} -- track all tiles for borderizing later

    -- ----------------------------------------------------------------
    -- PASS 1: Place ground tiles (water -> sand -> grass, inner wins)
    -- ----------------------------------------------------------------
    for dy = -outerRadius, outerRadius do
        for dx = -outerRadius, outerRadius do
            local x = cx + dx
            local y = cy + dy

            local groundId = nil

            -- Check from innermost to outermost
            if isInsideIsland(x, y, radius) then
                -- Grass (island interior)
                groundId = grassId
                grassCount = grassCount + 1
            elseif useBeach and isInsideIsland(x, y, radius + 1) then
                -- Sand beach ring
                groundId = sandId
                sandCount = sandCount + 1
            elseif isInsideIsland(x, y, radius + 3) then
                -- Water surrounding the island
                groundId = waterId
                waterCount = waterCount + 1
            end

            if groundId then
                local tile = map:getOrCreateTile(x, y, cz)
                if tile then
                    tile.ground = groundId
                    table.insert(allTiles, tile)
                end
            end
        end
    end

    -- ----------------------------------------------------------------
    -- PASS 2: Borderize all tiles for smooth transitions
    -- ----------------------------------------------------------------
    print("Borderizing tiles...")
    for _, tile in ipairs(allTiles) do
        tile:borderize()
    end

    -- ----------------------------------------------------------------
    -- PASS 3: Place decorative items on grass tiles
    -- ----------------------------------------------------------------
    if useDecos then
        -- Generate scattered positions using Poisson disk sampling
        local decoPositions =
            geo.randomScatter(cx - radius + 1, cy - radius + 1, cx + radius - 1, cy + radius - 1, 15, -- number of decoration attempts
                {
                    seed = useSeed + 100,
                    minDistance = 2
                })

        local decoItems = {ITEM_TREE, ITEM_TREE, ITEM_FLOWER, ITEM_FLOWER, ITEM_TREE}

        for i, pos in ipairs(decoPositions) do
            local px = math.floor(pos.x)
            local py = math.floor(pos.y)

            -- Only place on grass tiles (inside the island, not at center)
            local distFromCenter = math.sqrt((px - cx) ^ 2 + (py - cy) ^ 2)
            if isInsideIsland(px, py, radius - 1) and distFromCenter > 2 then
                local tile = map:getOrCreateTile(px, py, cz)
                if tile and tile.hasGround then
                    local itemId = decoItems[((i - 1) % #decoItems) + 1]
                    tile:addItem(itemId)
                    decoCount = decoCount + 1
                end
            end
        end
    end

    -- ----------------------------------------------------------------
    -- PASS 4: Place the sword near the center
    -- ----------------------------------------------------------------
    local swordTile = map:getOrCreateTile(cx + 1, cy, cz)
    if swordTile then
        swordTile:addItem(swordId)
        print(string.format("Sword placed at (%d, %d, %d)", cx + 1, cy, cz))
    end

    -- ----------------------------------------------------------------
    -- PASS 5: Place a campfire at the center
    -- ----------------------------------------------------------------
    local centerTile = map:getOrCreateTile(cx, cy, cz)
    if centerTile then
        centerTile:addItem(ITEM_CAMPFIRE)
        print(string.format("Campfire placed at (%d, %d, %d)", cx, cy, cz))
    end

    -- ----------------------------------------------------------------
    -- PASS 6: Place a sign post
    -- ----------------------------------------------------------------
    local signTile = map:getOrCreateTile(cx - 1, cy, cz)
    if signTile then
        signTile:addItem(ITEM_SIGN)
        print(string.format("Sign placed at (%d, %d, %d)", cx - 1, cy, cz))
    end

    -- ----------------------------------------------------------------
    -- PASS 7: Place monster with spawn
    -- ----------------------------------------------------------------
    -- Place the spawn a few tiles away from center
    local spawnX = cx + 3
    local spawnY = cy + 3

    -- Make sure spawn position is on the island
    if not isInsideIsland(spawnX, spawnY, radius - 1) then
        spawnX = cx + 2
        spawnY = cy + 2
    end

    local spawnTile = map:getOrCreateTile(spawnX, spawnY, cz)
    if spawnTile then
        -- Create the spawn area first
        spawnTile:setSpawn(SPAWN_RADIUS)

        -- Place the creature on the spawn tile
        spawnTile:setCreature(monsterName, spawnTime, Direction.SOUTH)

        print(string.format("%s spawn placed at (%d, %d, %d) with radius %d", monsterName, spawnX, spawnY, cz,
            SPAWN_RADIUS))
    end

    -- ----------------------------------------------------------------
    -- NOTE: NPC placement is NOT yet supported in the Lua API.
    -- The tile.npc and tile.spawnNpc fields exist in C++ but are not
    -- exposed to Lua. A future API update may add tile:setNpc().
    -- ----------------------------------------------------------------

end)

-- ============================================================================
-- MOVE CAMERA TO THE ISLAND
-- ============================================================================

app.setCameraPosition(cx, cy, cz)

-- ============================================================================
-- PRINT SUMMARY
-- ============================================================================

print("[Island Generator] Done!")
print(string.format("Grass tiles:%d", grassCount))
print(string.format("Sand tiles: %d", sandCount))
print(string.format("Water tiles:%d", waterCount))
print(string.format("Decorations:%d", decoCount))
print(string.format("Monster:%s (spawn time: %ds)", monsterName, spawnTime))
print("Sword, campfire, and sign placed near center.")
print("Tip: Press Ctrl+Z to undo if you want to revert.")
