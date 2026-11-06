-- @Title: Replace Action ID
-- @Description: Substitui Action IDs de um valor para outro (ex: 8000 para 100) no mapa ou na selecao.
-- @Author: André L. Morais
-- @Version: 1.0.0

if not app.hasMap() then
	app.alert({
		title = "Replace Action ID",
		text = "No map is currently open.",
	})
	return
end

local hasSelection = app.selection and not app.selection.isEmpty

local dlg = Dialog({
	title = "Replace Action ID",
	width = 380,
})

dlg:label({
	text = "Replaces Action IDs on grounds and items on the map.",
})
dlg:separator()

dlg:number({
	id = "fromAid",
	label = "From Action ID (old):",
	value = 100,
	min = 0,
	max = 65535,
})

dlg:number({
    id = "toAid",
    label = "To Action ID (new):",
	value = 100,
    onchange = function(d)
        local value = tonumber(d.data.toAid)
        local errorMessage = ""

        if value and value < 100 then
            errorMessage = "Invalid Action ID! Minimum allowed value is 100."
        elseif value and value > 65535 then
            errorMessage = "Invalid Action ID! Maximum allowed value is 65535."
        elseif value and value == tonumber(d.data.fromAid) then
    		errorMessage = "Destination ID cannot be the same as source ID."
        elseif not value and d.data.toAid ~= "" then
            errorMessage = "Please enter a valid numeric Action ID."
        end

        d:modify({
            aidWarning = {
                text = errorMessage,
                fgcolor = "#FF4444",
            }
        })
    end,
})

if hasSelection then
	dlg:separator()
	dlg:check({
		id = "selectionOnly",
		text = "Only in the selected area",
		value = false,
	})
end

dlg:separator()
dlg:button({
	id = "execute",
	text = "Replace",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:button({
	id = "cancel",
	text = "Cancel",
	onclick = function(d)
		d:close()
	end,
})

dlg:label({
	id = "aidWarning",
	text = "",
	fgcolor = "",
})

dlg:show()

local data = dlg.data
if not data or not data.execute then
	return
end

local fromAid = math.floor(data.fromAid or 0)
local toAid = math.floor(data.toAid or 0)
local selectionOnly = (hasSelection and data.selectionOnly == true)

if fromAid == toAid then
	app.alert({
		title = "Replace Action ID",
		text = "The Action ID of origin and destination are the same.",
	})
	return
end

if toAid ~= 0 and (toAid < 100 or toAid > 65535) then
	app.alert({
		title = "Invalid Action ID",
		text = "The Action ID of destination must be 0 (clear) or between 100 and 65535.",
	})
	return
end

local replacedItems = 0
local modifiedTiles = 0

app.transaction(string.format("Replace AID %d -> %d", fromAid, toAid), function()
	local function processTile(tile)
		if not tile then
			return
		end

		local tileModified = false

		-- 1. Check Ground
		if tile.hasGround and tile.ground then
			if tile.ground.actionId == fromAid then
				if not tileModified then
					tile.mapFlags = tile.mapFlags -- Notifies the transaction to support Undo (Ctrl+Z)
					tileModified = true
				end
				tile.ground.actionId = toAid
				replacedItems = replacedItems + 1
			end
		end

		-- 2. Check for items on the tile (doors, chests, levers, walls, etc.)
		local items = tile.items
		if items and #items > 0 then
			for i = 1, #items do
				local item = items[i]
				if item and item.actionId == fromAid then
					if not tileModified then
						tile.mapFlags = tile.mapFlags -- Notifies the transaction to support Undo (Ctrl+Z)
						tileModified = true
					end
					item.actionId = toAid
					replacedItems = replacedItems + 1
				end
			end
		end

		if tileModified then
			modifiedTiles = modifiedTiles + 1
		end
	end

	if selectionOnly then
		local selTiles = app.selection.tiles
		if selTiles then
			for i = 1, #selTiles do
				processTile(selTiles[i])
			end
		end
	else
		for tile in app.map.tiles do
			processTile(tile)
		end
	end
end)

local targetAreaText = selectionOnly and "in the selected area" or "on the entire map"
print(string.format("[Replace AID] %d item(s) changed in %d tile(s) %s (AID %d -> %d).", replacedItems, modifiedTiles, targetAreaText, fromAid, toAid))

app.alert({
	title = "Replace Action ID",
	text = string.format("Replacement completed successfully!\n\nItems changed: %d\nTiles modified: %d\nScope: %s\nChange: AID %d -> %d\n\n(You can undo the change using Ctrl+Z if necessary)", replacedItems, modifiedTiles, targetAreaText, fromAid, toAid),
})
