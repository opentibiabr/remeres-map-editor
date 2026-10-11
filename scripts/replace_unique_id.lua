-- @Title: Replace Unique ID
-- @Description: Substitui Unique IDs de um valor para outro (ex: 8000 para 100) no mapa ou na selecao.
-- @Author: Remere's Map Editor
-- @Version: 1.0.0

if not app.hasMap() then
	app.alert({
		title = "Replace Unique ID",
		text = "Nenhum mapa esta aberto no momento.",
	})
	return
end

local hasSelection = app.selection and not app.selection.isEmpty

local dlg = Dialog({
	title = "Replace Unique ID",
	width = 380,
})

dlg:label({
	text = "Substitui Unique IDs em grounds e itens no mapa.",
})
dlg:separator()

dlg:number({
	id = "fromUid",
	label = "De Unique ID (Antigo):",
	value = 8000,
	min = 0,
	max = 65535,
})

dlg:number({
	id = "toUid",
	label = "Para Unique ID (Novo):",
	value = 100,
	min = 0,
	max = 65535,
})

if hasSelection then
	dlg:separator()
	dlg:check({
		id = "selectionOnly",
		text = "Apenas na area selecionada",
		value = false,
	})
end


dlg:separator()
dlg:button({
	id = "execute",
	text = "Substituir",
	focus = true,
	onclick = function(d)
		d:close()
	end,
})
dlg:button({
	id = "cancel",
	text = "Cancelar",
	onclick = function(d)
		d:close()
	end,
})

dlg:show()

local data = dlg.data
if not data or not data.execute then
	return
end

local fromUid = math.floor(data.fromUid or 0)
local toUid = math.floor(data.toUid or 0)
local selectionOnly = (hasSelection and data.selectionOnly == true)

if fromUid == toUid then
	app.alert({
		title = "Replace Unique ID",
		text = "O Unique ID de origem e de destino sao iguais.",
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

		-- 1. Verifica Ground (Chao)
		if tile.hasGround and tile.ground then
			if tile.ground.uniqueId == fromUid then
				if not tileModified then
					tile.mapFlags = tile.mapFlags -- Notifica a transacao para suporte a Undo (Ctrl+Z)
					tileModified = true
				end
				tile.ground.uniqueId = toUid
				replacedItems = replacedItems + 1
			end
		end

		-- 2. Verifica Itens no Tile (portas, baus, alavancas, paredes, etc.)
		local items = tile.items
		if items and #items > 0 then
			for i = 1, #items do
				local item = items[i]
				if item and item.uniqueId == fromUid then
					if not tileModified then
						tile.mapFlags = tile.mapFlags -- Notifica a transacao para suporte a Undo (Ctrl+Z)
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

local targetAreaText = selectionOnly and "na area selecionada" or "no mapa inteiro"
print(string.format("[Replace UID] %d item(ns) alterado(s) em %d tile(s) %s (UID %d -> %d).", replacedItems, modifiedTiles, targetAreaText, fromUid, toUid))

app.alert({
	title = "Replace Unique ID",
	text = string.format("Substituicao concluida com sucesso!\n\nItens alterados: %d\nTiles modificados: %d\nEscopo: %s\nAlteracao: UID %d -> %d\n\n(Voce pode desfazer a alteracao usando Ctrl+Z se necessario)", replacedItems, modifiedTiles, targetAreaText, fromUid, toUid),
})
