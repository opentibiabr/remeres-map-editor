-- @Title: Replace Action ID
-- @Description: Substitui Action IDs de um valor para outro (ex: 8000 para 100) no mapa ou na selecao.
-- @Author: André L. Morais
-- @Version: 1.0.0

if not app.hasMap() then
	app.alert({
		title = "Replace Action ID",
		text = "Nenhum mapa esta aberto no momento.",
	})
	return
end

local hasSelection = app.selection and not app.selection.isEmpty

local dlg = Dialog({
	title = "Replace Action ID",
	width = 380,
})

dlg:label({
	text = "Substitui Action IDs em grounds e itens no mapa.",
})
dlg:separator()

dlg:number({
	id = "fromAid",
	label = "De Action ID (Antigo):",
	value = 8000,
	min = 0,
	max = 65535,
})

dlg:number({
	id = "toAid",
	label = "Para Action ID (Novo):",
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

local fromAid = math.floor(data.fromAid or 0)
local toAid = math.floor(data.toAid or 0)
local selectionOnly = (hasSelection and data.selectionOnly == true)

if fromAid == toAid then
	app.alert({
		title = "Replace Action ID",
		text = "O Action ID de origem e de destino sao iguais.",
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

		-- 1. Verifica Ground (Chao)
		if tile.hasGround and tile.ground then
			if tile.ground.actionId == fromAid then
				if not tileModified then
					tile.mapFlags = tile.mapFlags -- Notifica a transacao para suporte a Undo (Ctrl+Z)
					tileModified = true
				end
				tile.ground.actionId = toAid
				replacedItems = replacedItems + 1
			end
		end

		-- 2. Verifica Itens no Tile (portas, baus, alavancas, paredes, etc.)
		local items = tile.items
		if items and #items > 0 then
			for i = 1, #items do
				local item = items[i]
				if item and item.actionId == fromAid then
					if not tileModified then
						tile.mapFlags = tile.mapFlags -- Notifica a transacao para suporte a Undo (Ctrl+Z)
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

local targetAreaText = selectionOnly and "na area selecionada" or "no mapa inteiro"
print(string.format("[Replace AID] %d item(ns) alterado(s) em %d tile(s) %s (AID %d -> %d).", replacedItems, modifiedTiles, targetAreaText, fromAid, toAid))

app.alert({
	title = "Replace Action ID",
	text = string.format("Substituicao concluida com sucesso!\n\nItens alterados: %d\nTiles modificados: %d\nEscopo: %s\nAlteracao: AID %d -> %d\n\n(Voce pode desfazer a alteracao usando Ctrl+Z se necessario)", replacedItems, modifiedTiles, targetAreaText, fromAid, toAid),
})
