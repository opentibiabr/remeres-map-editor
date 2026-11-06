-- @Title: Replace Unique ID
-- @Description: Substitui Unique IDs de um valor para outro (ex: 8000 para 100) no mapa ou na selecao.
-- @Author: André L. Morais
-- @Version: 1.0.0

if not app.hasMap() then
	app.alert({
		title = "Replace Unique ID",
		text = "No map is currently open.",
	})
	return
end

local hasSelection = app.selection and not app.selection.isEmpty

local dlg = Dialog({
	title = "Replace Unique ID",
	width = 380,
})

dlg:label({
	text = "Replaces Unique IDs for grounds and items on the map.",
})
dlg:separator()

dlg:number({
	id = "fromUid",
	label = "From Unique ID (Old):",
	value = 1000,
	min = 0,
	max = 65535,
})

dlg:number({
    id = "toUid",
    label = "To Unique ID (new):",
	value = 1000,
    onchange = function(d)
        local value = tonumber(d.data.toUid)
        local errorMessage = ""

        if value and value < 1000 then
            errorMessage = "Invalid Unique ID! Minimum allowed value is 1000."
        elseif value and value > 65535 then
            errorMessage = "Invalid Unique ID! Maximum allowed value is 65535."
        elseif value and value == tonumber(d.data.fromUid) then
    		errorMessage = "Destination ID cannot be the same as source ID."
        elseif not value and d.data.toUid ~= "" then
            errorMessage = "Please enter a valid numeric Unique ID."
        end

        d:modify({
            uidWarning = {
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
	id = "uidWarning",
	text = "",
	fgcolor = "",
})

dlg:show()

local data = dlg.data
if not data or not data.execute then
	return
end

local fromUid = math.floor(data.fromUid or 0)
local toUid = math.floor(data.toUid or 0)
local selectionOnly = (hasSelection and data.selectionOnly == true)
local targetAreaText = selectionOnly and "in the selected area" or "on the entire map"

if fromUid == toUid then
	app.alert({
		title = "Replace Unique ID",
		text = "The Unique ID of origin and destination are the same.",
	})
	return
end

if toUid ~= 0 and (toUid < 1000 or toUid > 65535) then
	app.alert({
		title = "Replace Unique ID",
		text = "The Unique ID of destination must be 0 (clear) or between 1000 and 65535.",
	})
	return
end

local toUidAssigned = false
local fromUidMatches = 0

if selectionOnly then
	local selTiles = app.selection.tiles
	if selTiles then
		for i = 1, #selTiles do
			local tile = selTiles[i]
			if tile then
				if tile.hasGround and tile.ground and tile.ground.uniqueId == fromUid then
					fromUidMatches = fromUidMatches + 1
				end
				local items = tile.items
				if items and #items > 0 then
					for j = 1, #items do
						local item = items[j]
						if item and item.uniqueId == fromUid then
							fromUidMatches = fromUidMatches + 1
						end
					end
				end
			end
		end
	end

	if fromUidMatches > 1 then
		app.alert({
			title = "Replace Unique ID",
			text = string.format("Found %d items with Unique ID %d %s.\nUnique IDs must be unique. Operation cancelled.", fromUidMatches, fromUid, targetAreaText),
		})
		return
	end

	if toUid > 0 then
		for tile in app.map.tiles do
			if tile then
				if tile.hasGround and tile.ground and tile.ground.uniqueId == toUid then
					toUidAssigned = true
					break
				end
				local items = tile.items
				if items and #items > 0 then
					for j = 1, #items do
						local item = items[j]
						if item and item.uniqueId == toUid then
							toUidAssigned = true
							break
						end
					end
				end
				if toUidAssigned then
					break
				end
			end
		end
	end
else
	for tile in app.map.tiles do
		if tile then
			if tile.hasGround and tile.ground then
				local uid = tile.ground.uniqueId
				if toUid > 0 and uid == toUid then
					toUidAssigned = true
				end
				if uid == fromUid then
					fromUidMatches = fromUidMatches + 1
				end
			end
			local items = tile.items
			if items and #items > 0 then
				for j = 1, #items do
					local item = items[j]
					if item then
						local uid = item.uniqueId
						if toUid > 0 and uid == toUid then
							toUidAssigned = true
						end
						if uid == fromUid then
							fromUidMatches = fromUidMatches + 1
						end
					end
				end
			end
		end
	end
end

if toUidAssigned then
	app.alert({
		title = "Replace Unique ID",
		text = string.format("The Unique ID of destination (%d) is already in use on the map.\nUnique IDs must be unique. Operation cancelled.", toUid),
	})
	return
end

if fromUidMatches > 1 then
	app.alert({
		title = "Replace Unique ID",
		text = string.format("Found %d items with Unique ID %d %s.\nUnique IDs must be unique. Operation cancelled.", fromUidMatches, fromUid, targetAreaText),
	})
	return
end

local replacedItems = 0
local modifiedTiles = 0

app.transaction(string.format("Replace UID %d -> %d", fromUid, toUid), function()
	local function processTile(tile)
		if not tile then
			return
		end

		local tileModified = false

		-- 1. Check Ground
		if tile.hasGround and tile.ground then
			if tile.ground.uniqueId == fromUid then
				if not tileModified then
					tile.mapFlags = tile.mapFlags -- Notifies the transaction to support Undo (Ctrl+Z)
					tileModified = true
				end
				tile.ground.uniqueId = toUid
				replacedItems = replacedItems + 1
			end
		end

		-- 2. Check for items on the tile (doors, chests, levers, walls, etc.)
		local items = tile.items
		if items and #items > 0 then
			for i = 1, #items do
				local item = items[i]
				if item and item.uniqueId == fromUid then
					if not tileModified then
						tile.mapFlags = tile.mapFlags -- Notifies the transaction to support Undo (Ctrl+Z)
						tileModified = true
					end
					item.uniqueId = toUid
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

print(string.format("[Replace UID] %d item(s) altered in %d tile(s) %s (UID %d -> %d).", replacedItems, modifiedTiles, targetAreaText, fromUid, toUid))

app.alert({
	title = "Replace Unique ID",
	text = string.format("Replacement completed successfully!\n\nItems altered: %d\nTiles modified: %d\nScope: %s\nChange: UID %d -> %d\n\n(You can undo the change using Ctrl+Z if necessary)", replacedItems, modifiedTiles, targetAreaText, fromUid, toUid),
})
