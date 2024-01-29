
# *****************************************************************************
# Project canary map editor
# *****************************************************************************

project(canary-map-editor)

find_package(asio CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(GLUT REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(wxWidgets COMPONENTS html aui gl adv core net base CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(liblzma CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

# *****************************************************************************
# Options
# *****************************************************************************
option(TOGGLE_BIN_FOLDER "Use build/bin folder for generate compilation files" OFF)
option(OPTIONS_ENABLE_OPENMP "Enable Open Multi-Processing support." ON)
option(DEBUG_LOG "Enable Debug Log" OFF)

# === DEBUG LOG ===
# cmake -DDEBUG_LOG=ON ..
if(DEBUG_LOG)
	add_definitions(-DDEBUG_LOG=ON)
	log_option_enabled("DEBUG LOG")
else()
	log_option_disabled("DEBUG LOG")
endif(DEBUG_LOG)

if (MSVC)
	add_executable(${PROJECT_NAME} "" ../cmake/remeres.rc)

	if(BUILD_STATIC_LIBRARY)
		set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()

	target_compile_options(${PROJECT_NAME} PUBLIC /MP /FS /Zf /EHsc )
else()
	add_executable(${PROJECT_NAME} "")
endif()

# === OpenMP ===
if(OPTIONS_ENABLE_OPENMP)
	log_option_enabled("openmp")
	find_package(OpenMP)
	if(OpenMP_CXX_FOUND)
		target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
	endif()
else()
	log_option_disabled("openmp")
endif()

target_sources(${PROJECT_NAME}
	PRIVATE
	about_window.cpp
	action.cpp
	actions_history_window.cpp
	application.cpp
	artprovider.cpp
	basemap.cpp
	brush.cpp
	brush_tables.cpp
	browse_tile_window.cpp
	positionctrl.cpp
	carpet_brush.cpp
	client_assets.cpp
	common.cpp
	common_windows.cpp
	complexitem.cpp
	container_properties_window.cpp
	copybuffer.cpp
	monster_brush.cpp
	monster.cpp
	monsters.cpp
	dat_debug_view.cpp
	dcbutton.cpp
	doodad_brush.cpp
	editor.cpp
	editor_tabs.cpp
	eraser_brush.cpp
	find_item_window.cpp
	filehandle.cpp
	graphics.cpp
	ground_brush.cpp
	gui.cpp
	house_brush.cpp
	house.cpp
	house_exit_brush.cpp
	light_drawer.cpp
	iomap.cpp
	iomap_otbm.cpp
	iominimap.cpp
	item_attributes.cpp
	item.cpp
	items.cpp
	live_action.cpp
	live_client.cpp
	live_peer.cpp
	live_server.cpp
	live_socket.cpp
	live_tab.cpp
	main_menubar.cpp
	main_toolbar.cpp
	map.cpp
	map_display.cpp
	map_drawer.cpp
	map_region.cpp
	map_tab.cpp
	map_window.cpp
	materials.cpp
	minimap_window.cpp
	mkpch.cpp
	mt_rand.cpp
	net_connection.cpp
	npc.cpp
	npc_brush.cpp
	npcs.cpp
	numbertextctrl.cpp
	old_properties_window.cpp
	palette_brushlist.cpp
	palette_common.cpp
	palette_monster.cpp
	palette_house.cpp
	palette_npc.cpp
	palette_waypoints.cpp
	palette_zones.cpp
	palette_window.cpp
	pngfiles.cpp
	preferences.cpp
	process_com.cpp
	properties_window.cpp
	raw_brush.cpp
	replace_items_window.cpp
	result_window.cpp
	rme_net.cpp
	selection.cpp
	settings.cpp
	spawn_monster_brush.cpp
	spawn_monster.cpp
	spawn_npc.cpp
	spawn_npc_brush.cpp
	sprite_appearances.cpp
	table_brush.cpp
	templatemap76-74.cpp
	templatemap81.cpp
	templatemap854.cpp
	templatemapclassic.cpp
	tile.cpp
	tileset.cpp
	town.cpp
	updater.cpp
	wall_brush.cpp
	waypoint_brush.cpp
	waypoints.cpp
	welcome_dialog.cpp
	zone_brush.cpp
	zones.cpp
)

target_include_directories(${PROJECT_NAME}
	PRIVATE
	${CMAKE_SOURCE_DIR}/source
	${OPENGL_INCLUDE_DIR}
	${GLUT_INCLUDE_DIRS}
	${ZLIB_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
	${OPENGL_LIBRARIES}
	${GLUT_LIBRARIES}
	${ZLIB_LIBRARIES}
	fmt::fmt
	asio::asio
	nlohmann_json::nlohmann_json
	pugixml::pugixml
	wx::base wx::core wx::net wx::gl wx::html wx::aui wx::adv
	liblzma::liblzma
	OpenGL::GL
	protobuf
)

## Link compilation files to build/bin folder, else link to the main dir
if (TOGGLE_BIN_FOLDER)
	set_target_properties(${PROJECT_NAME}
			PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	)
else()
	set_target_properties(${PROJECT_NAME}
			PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/"
	)
endif()
