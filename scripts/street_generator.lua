-- @Title: Street Generator
-- @Description: Draws streets between waypoints using Bresenham lines
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
	print("[Streets] No map open.")
	return
end

local dlg = Dialog({
	title = "Street Generator",
	width = 400,
})
dlg:label({
	text = "Draw a street (Bresenham line) between two points.",
})
dlg:separator()
dlg:label({
	text = "Point A:",
})
dlg:number({
	id = "x1",
	label = "X:",
	value = 1000,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "y1",
	label = "Y:",
	value = 1000,
	min = 1,
	max = 65000,
})
dlg:newrow()
dlg:label({
	text = "Point B:",
})
dlg:number({
	id = "x2",
	label = "X:",
	value = 1020,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "y2",
	label = "Y:",
	value = 1010,
	min = 1,
	max = 65000,
})
dlg:newrow()
dlg:number({
	id = "cz",
	label = "Floor Z:",
	value = 7,
	min = 0,
	max = 15,
})
dlg:number({
	id = "streetWidth",
	label = "Street Width:",
	value = 3,
	min = 1,
	max = 10,
})
dlg:newrow()
dlg:input({
	id = "streetBrush",
	label = "Street Brush:",
	text = "sand",
})
dlg:input({
	id = "baseBrush",
	label = "Base Brush:",
	text = "grass",
})
dlg:newrow()
dlg:separator()
dlg:button({
	id = "go",
	text = "Generate Street",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:show()

local data = dlg.data
local x1 = math.floor(data.x1)
local y1 = math.floor(data.y1)
local x2 = math.floor(data.x2)
local y2 = math.floor(data.y2)
local cz = math.floor(data.cz)
local streetWidth = math.floor(data.streetWidth)
local streetBrush = data.streetBrush
local baseBrush = data.baseBrush

local linePoints = geo.bresenhamLine(x1, y1, x2, y2)
print(string.format("[Streets] Drawing street with %d center points, width %d...", #linePoints, streetWidth))

local map = app.map
local count = 0
local half = math.floor(streetWidth / 2)

app.transaction("Generate Street", function()
	local painted = {}

	for _, pt in ipairs(linePoints) do
		for dy = -half, half do
			for dx = -half, half do
				local x = pt.x + dx
				local y = pt.y + dy
				local key = x .. "," .. y
				if not painted[key] then
					painted[key] = true
					local tile = map:getOrCreateTile(x, y, cz)
					if tile then
						tile:applyBrush(streetBrush, false)
						count = count + 1
					end
				end
			end
		end
	end

	-- Borderize area
	local minX, maxX = math.min(x1, x2) - half - 2, math.max(x1, x2) + half + 2
	local minY, maxY = math.min(y1, y2) - half - 2, math.max(y1, y2) + half + 2
	for y = minY, maxY do
		for x = minX, maxX do
			local tile = map:getOrCreateTile(x, y, cz)
			if tile then
				tile:borderize()
			end
		end
	end
end)

app.setCameraPosition(math.floor((x1 + x2) / 2), math.floor((y1 + y2) / 2), cz)
print(string.format("[Streets] Done! %d tiles painted.", count))
