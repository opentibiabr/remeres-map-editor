cmake_minimum_required(VERSION 3.1)

project(rme)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(OpenGL REQUIRED)

# LibArchive disabled in compilation level by default, see "#define OTGZ_SUPPORT" in the "definitions.h" file
#if(APPLE)
#    set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
#endif()
# If you need use, enable this:
#find_package(LibArchive REQUIRED)
#${LibArchive_INCLUDE_DIRS} ${LibArchive_LIBRARIES}

find_package(asio CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(wxWidgets COMPONENTS html aui gl adv core net base CONFIG REQUIRED)

find_package(GLUT REQUIRED)
find_package(ZLIB REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(source/CMakeLists.txt)

add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

# === PRECOMPILED HEADER ===
target_precompile_headers(${PROJECT_NAME} PRIVATE source/main.h)

include_directories(
    ${OPENGL_INCLUDE_DIR}
    ${GLUT_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} 
    ${wxWidgets_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLUT_LIBRARIES}
    ${ZLIB_LIBRARIES}
    fmt::fmt
    asio::asio
    nlohmann_json::nlohmann_json
)
