-- @Title: Item Counter
-- @Description: Counts all items in a rectangular area and shows statistics
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
	print("[Counter] No map open.")
	return
end

local dlg = Dialog({
	title = "Item Counter",
	width = 400,
})
dlg:label({
	text = "Count items in a rectangular area.",
})
dlg:separator()
dlg:number({
	id = "x1",
	label = "From X:",
	value = 990,
	min = 0,
	max = 65000,
})
dlg:number({
	id = "y1",
	label = "From Y:",
	value = 990,
	min = 0,
	max = 65000,
})
dlg:newrow()
dlg:number({
	id = "x2",
	label = "To X:",
	value = 1010,
	min = 0,
	max = 65000,
})
dlg:number({
	id = "y2",
	label = "To Y:",
	value = 1010,
	min = 0,
	max = 65000,
})
dlg:newrow()
dlg:number({
	id = "z",
	label = "Floor Z:",
	value = 7,
	min = 0,
	max = 15,
})
dlg:newrow()
dlg:separator()
dlg:button({
	id = "go",
	text = "Count Items",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:show()

local data = dlg.data
local x1 = math.floor(math.min(data.x1, data.x2))
local y1 = math.floor(math.min(data.y1, data.y2))
local x2 = math.floor(math.max(data.x1, data.x2))
local y2 = math.floor(math.max(data.y1, data.y2))
local z = math.floor(data.z)

print(string.format("[Counter] Scanning area (%d,%d) to (%d,%d) on floor %d...", x1, y1, x2, y2, z))

local map = app.map
local totalItems = 0
local totalTiles = 0
local tilesWithGround = 0
local tilesWithItems = 0
local tilesWithCreatures = 0
local tilesWithSpawns = 0
local itemCounts = {} -- itemId -> count

for y = y1, y2 do
	for x = x1, x2 do
		local tile = map:getTile(x, y, z)
		if tile then
			totalTiles = totalTiles + 1

			if tile.hasGround then
				tilesWithGround = tilesWithGround + 1
				local groundId = tile.ground.id
				itemCounts[groundId] = (itemCounts[groundId] or 0) + 1
				totalItems = totalItems + 1
			end

			local items = tile.items
			if items and #items > 0 then
				tilesWithItems = tilesWithItems + 1
				for _, item in ipairs(items) do
					local id = item.id
					itemCounts[id] = (itemCounts[id] or 0) + 1
					totalItems = totalItems + 1
				end
			end

			if tile.hasCreature then
				tilesWithCreatures = tilesWithCreatures + 1
			end

			if tile.hasSpawn then
				tilesWithSpawns = tilesWithSpawns + 1
			end
		end
	end
end

-- Sort items by count (descending)
local sorted = {}
for id, count in pairs(itemCounts) do
	table.insert(sorted, {
		id = id,
		count = count,
	})
end
table.sort(sorted, function(a, b)
	return a.count > b.count
end)

-- Print results
print("[Counter] ========== RESULTS ==========")
print(string.format("  Area: (%d,%d) to (%d,%d), Floor: %d", x1, y1, x2, y2, z))
print(string.format("  Area size: %dx%d = %d sqm", x2 - x1 + 1, y2 - y1 + 1, (x2 - x1 + 1) * (y2 - y1 + 1)))
print(string.format("  Tiles found:       %d", totalTiles))
print(string.format("  Tiles with ground: %d", tilesWithGround))
print(string.format("  Tiles with items:  %d", tilesWithItems))
print(string.format("  Tiles with creatures: %d", tilesWithCreatures))
print(string.format("  Tiles with spawns: %d", tilesWithSpawns))
print(string.format("  Total items:       %d", totalItems))
print(string.format("  Unique item IDs:   %d", #sorted))
print("")
print("  Top 20 items:")
for i = 1, math.min(20, #sorted) do
	local entry = sorted[i]
	local name = Items.getName(entry.id)
	if name == "" then
		name = "(unknown)"
	end
	print(string.format('    #%d  ID: %d  "%s"  x%d', i, entry.id, name, entry.count))
end
print("[Counter] Done!")
