# *****************************************************************************
# Project protobuf
# *****************************************************************************
project(protobuf)

find_package(Protobuf REQUIRED)
find_package(Threads)

include_directories(${PROTOBUF_INCLUDE_DIRS})

file(GLOB ProtoFiles "${CMAKE_CURRENT_SOURCE_DIR}/**/*.proto"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

if(MSVC AND BUILD_STATIC_LIBRARY)
  add_library(${PROJECT_NAME} STATIC ${ProtoFiles})
else()
  add_library(${PROJECT_NAME} ${ProtoFiles})
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC protobuf::libprotobuf)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

if(MSVC AND BUILD_STATIC_LIBRARY)
  set_property(
    TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY
                                    "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(PROTOBUF_GENERATE_DEPENDENCIES)
if(TARGET protobuf::protoc)
  list(APPEND PROTOBUF_GENERATE_DEPENDENCIES $<TARGET_FILE:protobuf::protoc>)
elseif(Protobuf_PROTOC_EXECUTABLE)
  list(APPEND PROTOBUF_GENERATE_DEPENDENCIES ${Protobuf_PROTOC_EXECUTABLE})
elseif(PROTOBUF_PROTOC_EXECUTABLE)
  list(APPEND PROTOBUF_GENERATE_DEPENDENCIES ${PROTOBUF_PROTOC_EXECUTABLE})
endif()

protobuf_generate(TARGET ${PROJECT_NAME} LANGUAGE cpp DEPENDENCIES
                  ${PROTOBUF_GENERATE_DEPENDENCIES})
