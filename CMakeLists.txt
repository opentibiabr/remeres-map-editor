cmake_minimum_required(VERSION 3.22)

project(cmake)

# *****************************************************************************
# Vcpkg Configs
# *****************************************************************************

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
	set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
endif()

set(VCPKG_FEATURE_FLAGS "versions")
set(VCPKG_BUILD_TYPE "release")

# *****************************************************************************
# Set Configs
# *****************************************************************************

set(CMAKE_CXX_STANDARD 20)
set(GNUCXX_MINIMUM_VERSION 11)
set(MSVC_MINIMUM_VERSION "19.32")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

# Make will print more details
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# *****************************************************************************
# Options
# *****************************************************************************
option(OPTIONS_ENABLE_CCACHE "Enable ccache" OFF)
option(OPTIONS_ENABLE_SCCACHE "Use sccache to speed up compilation process" OFF)
option(OPTIONS_ENABLE_IPO "Check and Enable interprocedural optimization (IPO/LTO)" ON)

# *****************************************************************************
# Set Sanity Check
# *****************************************************************************

# === GCC Minimum Version ===
if (CMAKE_COMPILER_IS_GNUCXX)
	message("-- Compiler: GCC - Version: ${CMAKE_CXX_COMPILER_VERSION}")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS GNUCXX_MINIMUM_VERSION)
		message(FATAL_ERROR "GCC version must be at least ${GNUCXX_MINIMUM_VERSION}!")
	endif()
endif()

# === Minimum required version for visual studio ===
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	message("-- Compiler: Visual Studio - Version: ${CMAKE_CXX_COMPILER_VERSION}")
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MSVC_MINIMUM_VERSION)
		message(FATAL_ERROR "Visual Studio version must be at least ${MSVC_MINIMUM_VERSION}")
	endif()
endif()

# *****************************************************************************
# Append cmake search path
# *****************************************************************************
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# *****************************************************************************
# Include cmake tools
# *****************************************************************************
include(MessageColors)
include(LoggingHelper)

# *****************************************************************************
# Options Code
# *****************************************************************************

# === CCACHE ===
if(OPTIONS_ENABLE_CCACHE)
	find_program(CCACHE ccache)
	if(CCACHE)
		log_option_enabled("ccache")
		set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
	else()
		log_option_disabled("ccache")
	endif()
endif()

# === SCCACHE ===
if(OPTIONS_ENABLE_SCCACHE)
	find_program(SCCACHE_PATH sccache)
	if(SCCACHE_PATH)
		log_option_enabled("sccache")
		set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PATH})
		set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PATH})
	else()
		log_option_disabled("sccache")
	endif()
endif()

# === IPO ===
option(OPTIONS_ENABLE_IPO "Check and Enable interprocedural optimization (IPO/LTO)" ON)
if(OPTIONS_ENABLE_IPO)
	log_option_enabled("ipo")

	include(CheckIPOSupported)
	check_ipo_supported(RESULT result OUTPUT output)
	if(result)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		log_war("IPO is not supported: ${output}")
	endif()
else()
	log_option_disabled("ipo")
endif()

# *****************************************************************************
# Add source project
# *****************************************************************************
add_subdirectory(source)
