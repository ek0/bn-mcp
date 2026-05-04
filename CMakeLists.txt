cmake_minimum_required(VERSION 4.2 FATAL_ERROR)
include(FetchContent)

cmake_policy(SET CMP0067 NEW)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 23)

project(bn-mcp)

# Enable testing for the project
enable_testing()

if(WIN32)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Extract the currently installed binary ninja revision.
# If we don't do this, sometimes the dev branch will get out of sync when
# vector35 is rolling out an update and the project won't build.
file(READ "$ENV{APPDATA}/../Local/Programs/Vector35/BinaryNinja/api_REVISION.txt" FILE_CONTENT)
string(REGEX MATCH "(https:\/\/github\.com\/Vector35\/binaryninja-api\/tree\/)([a-fA-F0-9]+)" COMMIT_HASH ${FILE_CONTENT})
message(STATUS "Using binaryninja-api revision: ${CMAKE_MATCH_2}")

# Wish we could properly find_package the api...
FetchContent_Declare(binaryninja-api
  GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
  GIT_TAG ${CMAKE_MATCH_2}
  SYSTEM # So many warnings...
)

# nlohmann/json for JSON-RPC handling
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  SYSTEM
)

# cpp-httplib for the MCP HTTP transport
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE ON CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
  SYSTEM
)

FetchContent_MakeAvailable(binaryninja-api json httplib)

# Client library for other plugins to link against.
# Provides include paths, nlohmann_json, and the import lib.
add_library(bnmcp-client INTERFACE)
add_library(bnmcp::client ALIAS bnmcp-client)
target_include_directories(bnmcp-client INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)
target_link_libraries(bnmcp-client INTERFACE
	nlohmann_json::nlohmann_json
	bnmcp-plugin)

# MCP library for Binary Ninja
add_library(bnmcp STATIC
	# headers
	src/mcp_server.h
	src/bn_mcp.h

	# source files
	src/mcp_server.cpp
	src/bn_mcp.cpp
)

target_include_directories(bnmcp PUBLIC src)

target_link_libraries(bnmcp
	PUBLIC binaryninjaapi nlohmann_json::nlohmann_json
	PRIVATE httplib::httplib)

# Plugin DLL
add_library(bnmcp-plugin SHARED
	# SOURCES
	src/plugin.cpp
)

target_include_directories(bnmcp-plugin PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)
target_compile_definitions(bnmcp-plugin PRIVATE BNMCP_EXPORTS)
target_link_libraries(bnmcp-plugin PRIVATE bnmcp)
install(TARGETS bnmcp-plugin DESTINATION "$ENV{APPDATA}\\Binary Ninja\\plugins")

# Example plugin using the client API
add_library(bnmcp-example SHARED
	examples/list_functions/plugin.cpp
)
target_link_libraries(bnmcp-example PRIVATE binaryninjaapi bnmcp::client)
install(TARGETS bnmcp-example DESTINATION "$ENV{APPDATA}\\Binary Ninja\\plugins")