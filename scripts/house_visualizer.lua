-- @Title: House Visualizer
-- @Description: Draws colored overlays for house tiles, each house gets a unique color
-- @Author: VLL Systems
-- @Version: 1.0.0
-- @AutoRun: true
if not app.hasMap() then
	return
end

-- Generate a deterministic color from a house ID
local function houseColor(houseId)
	-- Simple hash to spread colors
	local h = (houseId * 2654435761) % 360 -- golden ratio hash -> hue
	-- Convert HSV (h, 0.6, 0.9) to RGB
	local s = 0.6
	local v = 0.9
	local c = v * s
	local x = c * (1 - math.abs((h / 60) % 2 - 1))
	local m = v - c
	local r, g, b = 0, 0, 0

	if h < 60 then
		r, g, b = c, x, 0
	elseif h < 120 then
		r, g, b = x, c, 0
	elseif h < 180 then
		r, g, b = 0, c, x
	elseif h < 240 then
		r, g, b = 0, x, c
	elseif h < 300 then
		r, g, b = x, 0, c
	else
		r, g, b = c, 0, x
	end

	return {
		r = math.floor((r + m) * 255),
		g = math.floor((g + m) * 255),
		b = math.floor((b + m) * 255),
		a = 90,
	}
end

app.mapView.addOverlay("house_visualizer", {
	enabled = true,
	order = 5,

	ondraw = function(ctx)
		local view = ctx.view
		local map = app.map
		if not map then
			return
		end

		local z = view.z

		for y = view.y1, view.y2 do
			for x = view.x1, view.x2 do
				local tile = map:getTile(x, y, z)
				if tile then
					local hid = tile.houseId
					if hid and hid > 0 then
						local col = houseColor(hid)
						ctx.rect({
							x = x,
							y = y,
							z = z,
							w = 1,
							h = 1,
							filled = true,
							color = col,
						})
					end
				end
			end
		end
	end,

	onhover = function(info)
		local tile = info.tile
		if not tile then
			return nil
		end

		local hid = tile.houseId
		if hid and hid > 0 then
			return {
				tooltip = "House ID: " .. hid,
				highlight = {
					x = tile.x,
					y = tile.y,
					z = tile.z,
					w = 1,
					h = 1,
					filled = true,
					width = 2,
					color = {
						r = 255,
						g = 255,
						b = 255,
						a = 120,
					},
				},
			}
		end

		return nil
	end,
})

app.mapView.registerShow("House Overlay", "house_visualizer", {
	enabled = true,
})

print("[House Visualizer] Overlay registered. Toggle via View menu.")
