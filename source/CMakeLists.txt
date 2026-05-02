# *****************************************************************************
# Project remeres
# *****************************************************************************
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  project(canary-map-editor-debug LANGUAGES CXX)
else()
  project(canary-map-editor LANGUAGES CXX)
endif()

find_package(asio CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_truetype.h")
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(
  wxWidgets
  COMPONENTS html
             aui
             gl
             adv
             core
             net
             base
             CONFIG
  REQUIRED)
if(DEFINED VCPKG_TARGET_TRIPLET)
  unset(ZLIB_LIBRARY CACHE)
  unset(ZLIB_LIBRARY_DEBUG CACHE)
  unset(ZLIB_LIBRARY_RELEASE CACHE)
endif()
find_package(ZLIB REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(liblzma CONFIG REQUIRED)
find_package(Protobuf REQUIRED)
find_package(cpr CONFIG REQUIRED)

include(FetchContent)
FetchContent_Declare(
  FastNoiseLite
  GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
  GIT_TAG v1.1.1)
FetchContent_MakeAvailable(FastNoiseLite)

# *****************************************************************************
# Options
# *****************************************************************************
option(TOGGLE_BIN_FOLDER "Use build/bin folder for generate compilation files"
       OFF)
option(OPTIONS_ENABLE_OPENMP "Enable Open Multi-Processing support." ON)
option(DEBUG_LOG "Enable Debug Log" OFF)
option(SPEED_UP_BUILD_UNITY "Compile using build unity for speed up build" ON)

# LibArchive disabled in compilation level by default, see "#define
# OTGZ_SUPPORT" in the "definitions.h" file if(APPLE) set(CMAKE_PREFIX_PATH
# /usr/local/opt/libarchive) endif() If you need use, enable this:
# find_package(LibArchive REQUIRED) ${LibArchive_INCLUDE_DIRS}
# ${LibArchive_LIBRARIES}

# Build static libs
if(BUILD_STATIC_LIBRARY)
  log_option_enabled("STATIC_LIBRARY")

  if(UNIX AND NOT APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  elseif(APPLE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dylib")
  endif()
else()
  log_option_disabled("STATIC_LIBRARY")
endif()

# === DEBUG LOG ===
# cmake -DDEBUG_LOG=ON ..
if(DEBUG_LOG)
  add_definitions(-DDEBUG_LOG=ON)
  log_option_enabled("DEBUG LOG")
else()
  log_option_disabled("DEBUG LOG")
endif(DEBUG_LOG)

if(MSVC)
  add_executable(${PROJECT_NAME} "" ../cmake/remeres.rc)

  if(BUILD_STATIC_LIBRARY)
    set_property(
      TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY
                                      "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  target_compile_options(${PROJECT_NAME} PUBLIC /MP /FS /Zf /EHsc /bigobj)
else()
  add_executable(${PROJECT_NAME} "")
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}-x64")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE RME_CMAKE_BUILD=1)

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

# === IPO ===
if(OPTIONS_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT result OUTPUT output)
  if(result)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION
                                                 TRUE)
  else()
    message(WARNING "IPO is not supported: ${output}")
  endif()
endif()

# Lua includes (target-specific)
if(NOT LUA_LIB_TARGET)
  if(TARGET lua::lua)
    set(LUA_LIB_TARGET lua::lua)
  elseif(TARGET lua)
    set(LUA_LIB_TARGET lua)
  elseif(TARGET unofficial::lua::lua)
    set(LUA_LIB_TARGET unofficial::lua::lua)
  elseif(LUA_LIBRARIES)
    set(LUA_LIB_TARGET ${LUA_LIBRARIES})
  endif()
endif()

if(LUA_LIB_TARGET)
  if(TARGET "${LUA_LIB_TARGET}")
    get_target_property(VCPKG_LUA_INCLUDES ${LUA_LIB_TARGET}
                        INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  if(VCPKG_LUA_INCLUDES)
    target_include_directories(${PROJECT_NAME} PRIVATE ${VCPKG_LUA_INCLUDES})
  elseif(LUA_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
  endif()
  target_compile_definitions(${PROJECT_NAME} PRIVATE SOL_USING_CXX_LUA_HPP=1
                                                     SOL_ALL_SAFETIES_ON=1)
endif()

# === UNITY BUILD (compile time reducer) ===
if(SPEED_UP_BUILD_UNITY)
  set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ON)
  set_source_files_properties(gl_renderer.cpp
                              PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  log_option_enabled("Build unity for speed up compilation")
else()
  # === PRECOMPILED HEADER ===
  target_precompile_headers(${PROJECT_NAME} PRIVATE main.h)
endif()

target_sources(
  ${PROJECT_NAME}
  PRIVATE about_window.cpp
          action.cpp
          actions_history_window.cpp
          add_item_window.cpp
          add_tileset_window.cpp
          application.cpp
          artprovider.cpp
          basemap.cpp
          bitmap_to_map_window.cpp
          bitmap_to_map_converter.cpp
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
          gl_renderer.cpp
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
          lua/lua_engine.cpp
          lua/lua_script.cpp
          lua/lua_script_manager.cpp
          lua/lua_api.cpp
          lua/lua_api_app.cpp
          lua/lua_api_position.cpp
          lua/lua_api_item.cpp
          lua/lua_api_tile.cpp
          lua/lua_api_map.cpp
          lua/lua_api_selection.cpp
          lua/lua_dialog.cpp
          lua/lua_api_color.cpp
          lua/lua_api_creature.cpp
          lua/lua_api_brush.cpp
          lua/lua_api_image.cpp
          lua/lua_api_geo.cpp
          lua/lua_api_http.cpp
          lua/lua_api_noise.cpp
          lua/lua_api_json.cpp
          lua/lua_api_algo.cpp
          lua/lua_scripts_window.cpp
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
          stb_truetype_impl.cpp
          table_brush.cpp
          templatemap76-74.cpp
          templatemap81.cpp
          templatemap854.cpp
          templatemapclassic.cpp
          tile.cpp
          tileset.cpp
          tileset_window.cpp
          town.cpp
          updater.cpp
          wall_brush.cpp
          waypoint_brush.cpp
          waypoints.cpp
          welcome_dialog.cpp
          zone_brush.cpp
          zones.cpp)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(
    lua/lua_api_noise.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
               COMPILE_OPTIONS "-Wno-aggressive-loop-optimizations")
endif()

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/source ${OPENGL_INCLUDE_DIR}
                          ${ZLIB_INCLUDE_DIR} ${STB_INCLUDE_DIRS})

target_include_directories(${PROJECT_NAME} SYSTEM
                           PRIVATE ${fastnoiselite_SOURCE_DIR}/Cpp)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ${OPENGL_LIBRARIES}
          ZLIB::ZLIB
          glad::glad
          fmt::fmt
          asio::asio
          nlohmann_json::nlohmann_json
          pugixml::pugixml
          wx::base
          wx::core
          wx::net
          wx::gl
          wx::html
          wx::aui
          wx::adv
          liblzma::liblzma
          OpenGL::GL
          protobuf
          ${LUA_LIB_TARGET}
          sol2::sol2
          cpr::cpr
          $<$<PLATFORM_ID:Linux>:xcb>
          $<$<PLATFORM_ID:Windows>:psapi>)

# Link compilation files to build/bin folder, else link to the main dir
if(TOGGLE_BIN_FOLDER)
  set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                   "${CMAKE_BINARY_DIR}/bin")
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                   "${CMAKE_SOURCE_DIR}/")
endif()
