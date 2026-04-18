-- @Title: Spawn Visualizer  
-- @Description: Draws spawn radius circles as an overlay on the map  
-- @Author: VLL Systems  
-- @Version: 1.0.0  
-- @AutoRun: true  
if not app.hasMap() then
    return
end

-- Register the overlay  
app.mapView.addOverlay("spawn_visualizer", {
    enabled = true,
    order = 10,

    ondraw = function(ctx)
        local view = ctx.view
        local map = app.map
        if not map then
            return
        end

        local z = view.z

        -- Scan visible area for spawns  
        for y = view.y1, view.y2 do
            for x = view.x1, view.x2 do
                local tile = map:getTile(x, y, z)
                if tile and tile.hasSpawn then
                    local spawn = tile.spawn
                    local radius = spawn.size

                    -- Draw spawn radius as a rectangle outline (circle approximation)  
                    ctx.rect({
                        x = x - radius,
                        y = y - radius,
                        z = z,
                        w = radius * 2 + 1,
                        h = radius * 2 + 1,
                        filled = false,
                        width = 1,
                        style = "dashed",
                        color = {
                            r = 255,
                            g = 100,
                            b = 100,
                            a = 140
                        }
                    })

                    -- Draw center marker  
                    ctx.rect({
                        x = x,
                        y = y,
                        z = z,
                        w = 1,
                        h = 1,
                        filled = true,
                        color = {
                            r = 255,
                            g = 50,
                            b = 50,
                            a = 180
                        }
                    })

                    -- Draw creature name if present  
                    if tile.hasCreature then
                        local creature = tile.creature
                        if creature then
                            ctx.text({
                                x = x,
                                y = y - 1,
                                z = z,
                                text = creature.name,
                                color = {
                                    r = 255,
                                    g = 200,
                                    b = 200,
                                    a = 220
                                }
                            })
                        end
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

        if tile.hasSpawn then
            local spawn = tile.spawn
            local text = "Spawn (radius: " .. spawn.size .. ")"
            if tile.hasCreature then
                local creature = tile.creature
                text = text .. "\n" .. creature.name .. " (spawn: " .. creature.spawnTime .. "s)"
            end
            return {
                tooltip = text,
                highlight = {
                    x = tile.x - spawn.size,
                    y = tile.y - spawn.size,
                    z = tile.z,
                    w = spawn.size * 2 + 1,
                    h = spawn.size * 2 + 1,
                    filled = false,
                    width = 2,
                    color = {
                        r = 255,
                        g = 255,
                        b = 0,
                        a = 180
                    }
                }
            }
        end

        return nil
    end
})

-- Register toggle in View menu  
app.mapView.registerShow("Spawn Radius Overlay", "spawn_visualizer", {
    enabled = true
})

print("[Spawn Visualizer] Overlay registered. Toggle via View menu.")
