-- @Title: Map Stats Exporter
-- @Description: Iterates all map tiles and exports statistics to JSON
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
	print("[Stats] No map open.")
	return
end

local dlg = Dialog({
	title = "Map Stats Exporter",
	width = 400,
})
dlg:label({
	text = "Scan the entire map and export statistics to JSON.",
})
dlg:separator()
dlg:input({
	id = "filename",
	label = "Output file:",
	text = "map_stats",
})
dlg:newrow()
dlg:separator()
dlg:button({
	id = "go",
	text = "Export Stats",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:show()

local data = dlg.data
local filename = data.filename

local map = app.map

print("[Stats] Scanning entire map...")
print(string.format("  Map: %s (%dx%d)", map.name, map.width, map.height))

local stats = {
	mapName = map.name,
	mapWidth = map.width,
	mapHeight = map.height,
	totalTiles = 0,
	tilesWithGround = 0,
	tilesWithItems = 0,
	tilesWithCreatures = 0,
	tilesWithSpawns = 0,
	tilesWithHouse = 0,
	totalItems = 0,
	totalCreatures = 0,
	totalSpawns = 0,
	floorDistribution = {},
	topItems = {},
	creatureNames = {},
	houseIds = {},
}

local itemCounts = {}
local creatureCounts = {}
local floorCounts = {}
local houseTileCounts = {}
local tileCount = 0

for tile in map.tiles do
	tileCount = tileCount + 1
	stats.totalTiles = stats.totalTiles + 1

	local z = tile.z
	floorCounts[z] = (floorCounts[z] or 0) + 1

	if tile.hasGround then
		stats.tilesWithGround = stats.tilesWithGround + 1
		local gid = tile.ground.id
		itemCounts[gid] = (itemCounts[gid] or 0) + 1
		stats.totalItems = stats.totalItems + 1
	end

	local items = tile.items
	if items and #items > 0 then
		stats.tilesWithItems = stats.tilesWithItems + 1
		for _, item in ipairs(items) do
			local id = item.id
			itemCounts[id] = (itemCounts[id] or 0) + 1
			stats.totalItems = stats.totalItems + 1
		end
	end

	if tile.hasCreature then
		stats.tilesWithCreatures = stats.tilesWithCreatures + 1
		stats.totalCreatures = stats.totalCreatures + 1
		local creature = tile.creature
		if creature then
			local name = creature.name
			creatureCounts[name] = (creatureCounts[name] or 0) + 1
		end
	end

	if tile.hasSpawn then
		stats.tilesWithSpawns = stats.tilesWithSpawns + 1
		stats.totalSpawns = stats.totalSpawns + 1
	end

	local hid = tile.houseId
	if hid and hid > 0 then
		stats.tilesWithHouse = stats.tilesWithHouse + 1
		houseTileCounts[hid] = (houseTileCounts[hid] or 0) + 1
	end

	-- Yield every 10000 tiles to keep UI responsive
	if tileCount % 10000 == 0 then
		app.yield()
	end
end

-- Floor distribution
for z = 0, 15 do
	if floorCounts[z] then
		table.insert(stats.floorDistribution, {
			floor = z,
			tiles = floorCounts[z],
		})
	end
end

-- Top 30 items
local sortedItems = {}
for id, count in pairs(itemCounts) do
	local name = Items.getName(id)
	if name == "" then
		name = "unknown"
	end
	table.insert(sortedItems, {
		id = id,
		name = name,
		count = count,
	})
end
table.sort(sortedItems, function(a, b)
	return a.count > b.count
end)
for i = 1, math.min(30, #sortedItems) do
	table.insert(stats.topItems, sortedItems[i])
end

-- Creature counts
local sortedCreatures = {}
for name, count in pairs(creatureCounts) do
	table.insert(sortedCreatures, {
		name = name,
		count = count,
	})
end
table.sort(sortedCreatures, function(a, b)
	return a.count > b.count
end)
stats.creatureNames = sortedCreatures

-- House stats
local sortedHouses = {}
for hid, count in pairs(houseTileCounts) do
	table.insert(sortedHouses, {
		houseId = hid,
		tiles = count,
	})
end
table.sort(sortedHouses, function(a, b)
	return a.tiles > b.tiles
end)
stats.houseIds = sortedHouses

-- Save to JSON
local storage = app.storage(filename)
local ok = storage:save(stats)

if ok then
	print(string.format("[Stats] Exported to: %s", storage.path))
else
	print("[Stats] ERROR: Failed to save JSON file!")
end

-- Print summary
print("[Stats] ========== SUMMARY ==========")
print(string.format("  Total tiles:     %d", stats.totalTiles))
print(string.format("  Total items:     %d", stats.totalItems))
print(string.format("  Total creatures: %d", stats.totalCreatures))
print(string.format("  Total spawns:    %d", stats.totalSpawns))
print(string.format("  House tiles:     %d", stats.tilesWithHouse))
print(string.format("  Unique items:    %d", #sortedItems))
print(string.format("  Unique creatures:%d", #sortedCreatures))
print(string.format("  Unique houses:   %d", #sortedHouses))
print("[Stats] Done!")
