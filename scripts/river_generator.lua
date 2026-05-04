-- @Title: River Generator
-- @Description: Generates a river using Bezier curves
-- @Author: VLL Systems
-- @Version: 1.0.0
if not app.hasMap() then
	print("[River] No map open.")
	return
end

local dlg = Dialog({
	title = "River Generator",
	width = 400,
})
dlg:label({
	text = "Generate a river using Bezier curves.",
})
dlg:separator()
dlg:label({
	text = "Start Point:",
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
	text = "Control Point (curve):",
})
dlg:number({
	id = "mx",
	label = "X:",
	value = 1015,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "my",
	label = "Y:",
	value = 1005,
	min = 1,
	max = 65000,
})
dlg:newrow()
dlg:label({
	text = "End Point:",
})
dlg:number({
	id = "x2",
	label = "X:",
	value = 1030,
	min = 1,
	max = 65000,
})
dlg:number({
	id = "y2",
	label = "Y:",
	value = 1015,
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
	id = "riverWidth",
	label = "River Width:",
	value = 3,
	min = 1,
	max = 10,
})
dlg:number({
	id = "steps",
	label = "Curve Steps:",
	value = 60,
	min = 10,
	max = 200,
})
dlg:newrow()
dlg:input({
	id = "waterBrush",
	label = "Water Brush:",
	text = "sea",
})
dlg:newrow()
dlg:separator()
dlg:button({
	id = "go",
	text = "Generate River",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:show()

local data = dlg.data
local cz = math.floor(data.cz)
local riverWidth = math.floor(data.riverWidth)
local steps = math.floor(data.steps)
local waterBrush = data.waterBrush

local controlPoints = {
	{
		x = math.floor(data.x1),
		y = math.floor(data.y1),
	},
	{
		x = math.floor(data.mx),
		y = math.floor(data.my),
	},
	{
		x = math.floor(data.x2),
		y = math.floor(data.y2),
	},
}

local curvePoints = geo.bezierCurve(controlPoints, steps)
print(string.format("[River] Drawing river with %d curve points, width %d...", #curvePoints, riverWidth))

local map = app.map
local count = 0
local half = math.floor(riverWidth / 2)

app.transaction("Generate River", function()
	local painted = {}

	for _, pt in ipairs(curvePoints) do
		for dy = -half, half do
			for dx = -half, half do
				local dist = math.sqrt(dx * dx + dy * dy)
				if dist <= half + 0.5 then
					local x = math.floor(pt.x) + dx
					local y = math.floor(pt.y) + dy
					local key = x .. "," .. y
					if not painted[key] then
						painted[key] = true
						local tile = map:getOrCreateTile(x, y, cz)
						if tile then
							tile:applyBrush(waterBrush, false)
							count = count + 1
						end
					end
				end
			end
		end
	end

	-- Borderize
	local minX = math.min(data.x1, data.x2, data.mx) - half - 3
	local maxX = math.max(data.x1, data.x2, data.mx) + half + 3
	local minY = math.min(data.y1, data.y2, data.my) - half - 3
	local maxY = math.max(data.y1, data.y2, data.my) + half + 3
	for y = minY, maxY do
		for x = minX, maxX do
			local tile = map:getOrCreateTile(x, y, cz)
			if tile then
				tile:borderize()
			end
		end
	end
end)

local midX = math.floor((data.x1 + data.x2) / 2)
local midY = math.floor((data.y1 + data.y2) / 2)
app.setCameraPosition(midX, midY, cz)
print(string.format("[River] Done! %d water tiles placed.", count))
