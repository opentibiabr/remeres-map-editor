cmake_minimum_required(VERSION 3.17)

project(remeres)

# *****************************************************************************
# CMake Features
# *****************************************************************************
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED ON)
set(Boost_NO_WARN_NEW_VERSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# *****************************************************************************
# Project
# *****************************************************************************
add_executable(${PROJECT_NAME} cmake/rme.rc)

# *****************************************************************************
# Link options
# *****************************************************************************
target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")

# *****************************************************************************
# Packages / Libs
# *****************************************************************************
find_package(Boost COMPONENTS thread system REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Flatbuffers CONFIG REQUIRED)
find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)
find_package(GLUT REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibArchive REQUIRED)
if (MSVC)
	find_package(Threads REQUIRED)
endif()
if(APPLE)
	set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
endif()

set(MY_URL "https://github.com/opentibiabr/libs/raw/main/KnaryLib.lib")
set(MY_DOWNLOAD_PATH "source/libs/KnaryLib.lib")

if (NOT EXISTS "${MY_DOWNLOAD_PATH}")
	file(DOWNLOAD "${MY_URL}" "${MY_DOWNLOAD_PATH}")
endif()

include(GNUInstallDirs)

include(${wxWidgets_USE_FILE})
## Target sources
include(source/CMakeLists.txt)

if (MSVC)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
	elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
	endif()
endif()

include_directories(${PROJECT_NAME}
PRIVATE
	${Boost_INCLUDE_DIRS}
	${OPENGL_INCLUDE_DIR}
)
target_link_libraries(${PROJECT_NAME}
	${Boost_LIBRARIES}
	${wxWidgets_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT}
	${OPENGL_LIBRARIES}
	flatbuffers::flatbuffers
	GLUT::GLUT
	ZLIB::ZLIB
	LibArchive::LibArchive
)

set_target_properties(${PROJECT_NAME}
	PROPERTIES
	  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
