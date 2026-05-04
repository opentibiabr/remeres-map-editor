-- @Title: City Lot Generator
-- @Description: Generates city lots using Voronoi diagram
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
	print("[City] No map open.")
	return
end

local dlg = Dialog({
	title = "City Lot Generator",
	width = 400,
})
dlg:label({
	text = "Generate city lots using Voronoi regions.",
})
dlg:separator()
dlg:number({
	id = "cx",
	label = "Start X:",
	value = 1000,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "cy",
	label = "Start Y:",
	value = 1000,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "cz",
	label = "Floor Z:",
	value = 7,
	min = 0,
	max = 15,
})
dlg:newrow()
dlg:number({
	id = "width",
	label = "Width:",
	value = 40,
	min = 10,
	max = 200,
})
dlg:number({
	id = "height",
	label = "Height:",
	value = 40,
	min = 10,
	max = 200,
})
dlg:newrow()
dlg:number({
	id = "lots",
	label = "Number of Lots:",
	value = 8,
	min = 2,
	max = 30,
})
dlg:number({
	id = "seed",
	label = "Seed:",
	value = 42,
	min = 1,
	max = 99999,
})
dlg:newrow()
dlg:input({
	id = "groundBrush",
	label = "Ground Brush:",
	text = "grass",
})
dlg:input({
	id = "streetBrush",
	label = "Street Brush:",
	text = "sand",
})
dlg:newrow()
dlg:separator()
dlg:button({
	id = "go",
	text = "Generate",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:show()

local data = dlg.data
local cx = math.floor(data.cx)
local cy = math.floor(data.cy)
local cz = math.floor(data.cz)
local w = math.floor(data.width)
local h = math.floor(data.height)
local numLots = math.floor(data.lots)
local seed = math.floor(data.seed)
local groundBrush = data.groundBrush
local streetBrush = data.streetBrush

-- Generate seed points for Voronoi
local seedPoints = geo.randomScatter(1, 1, w, h, numLots, {
	seed = seed,
	minDistance = 5,
})

print(string.format("[City] Generating %d lots in %dx%d area...", #seedPoints, w, h))

local voronoiGrid = algo.voronoi(w, h, seedPoints)

local map = app.map
local lotCount = 0
local streetCount = 0

app.transaction("Generate City Lots", function()
	-- Paint ground and detect borders between regions (streets)
	for gy = 1, h do
		for gx = 1, w do
			local x = cx + gx - 1
			local y = cy + gy - 1
			local region = voronoiGrid[gy][gx]
			local tile = map:getOrCreateTile(x, y, cz)
			if tile then
				-- Check if this is a border between regions
				local isBorder = false
				if gx > 1 and voronoiGrid[gy][gx - 1] ~= region then
					isBorder = true
				end
				if gx < w and voronoiGrid[gy][gx + 1] ~= region then
					isBorder = true
				end
				if gy > 1 and voronoiGrid[gy - 1][gx] ~= region then
					isBorder = true
				end
				if gy < h and voronoiGrid[gy + 1][gx] ~= region then
					isBorder = true
				end

				if isBorder then
					tile:applyBrush(streetBrush, false)
					streetCount = streetCount + 1
				else
					tile:applyBrush(groundBrush, false)
					lotCount = lotCount + 1
				end
			end
		end
	end

	-- Borderize
	for gy = -1, h + 2 do
		for gx = -1, w + 2 do
			local tile = map:getOrCreateTile(cx + gx - 1, cy + gy - 1, cz)
			if tile then
				tile:borderize()
			end
		end
	end
end)

app.setCameraPosition(cx + math.floor(w / 2), cy + math.floor(h / 2), cz)
print(string.format("[City] Done! Lots: %d tiles, Streets: %d tiles", lotCount, streetCount))
