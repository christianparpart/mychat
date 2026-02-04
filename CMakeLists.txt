cmake_minimum_required(VERSION 3.25)

include(cmake/Version.cmake)

project(mychat
    VERSION ${MYCHAT_VERSION}
    LANGUAGES C CXX
    DESCRIPTION "CLI chatbot with local LLM inference, voice input, and MCP tool integration"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/EnableCcache.cmake)
include(cmake/PedanticCompiler.cmake)
include(cmake/Sanitizers.cmake)

# --------------------------------------------------------------------------
# CPM dependency manager
# --------------------------------------------------------------------------
set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
        "${CPM_DOWNLOAD_LOCATION}"
    )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
CPMAddPackage(
    NAME nlohmann_json
    VERSION 3.11.3
    GITHUB_REPOSITORY nlohmann/json
    OPTIONS "JSON_BuildTests OFF"
)

CPMAddPackage(
    NAME CLI11
    VERSION 2.4.2
    GITHUB_REPOSITORY CLIUtils/CLI11
    OPTIONS "CLI11_BUILD_TESTS OFF" "CLI11_BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME Catch2
    VERSION 3.7.1
    GITHUB_REPOSITORY catchorg/Catch2
    OPTIONS "CATCH_BUILD_TESTING OFF" "CATCH_INSTALL_DOCS OFF"
)

CPMAddPackage(
    NAME llama.cpp
    VERSION 0.0.1
    GITHUB_REPOSITORY ggml-org/llama.cpp
    GIT_TAG master
    OPTIONS
        "LLAMA_BUILD_TESTS OFF"
        "LLAMA_BUILD_EXAMPLES OFF"
        "LLAMA_BUILD_SERVER OFF"
        "LLAMA_CURL OFF"
)

CPMAddPackage(
    NAME whisper.cpp
    VERSION 0.0.1
    GITHUB_REPOSITORY ggerganov/whisper.cpp
    GIT_TAG master
    OPTIONS
        "WHISPER_BUILD_TESTS OFF"
        "WHISPER_BUILD_EXAMPLES OFF"
        "BUILD_SHARED_LIBS OFF"
)

# --------------------------------------------------------------------------
# piper1-gpl (TTS via libpiper C API)
# --------------------------------------------------------------------------
CPMAddPackage(
    NAME piper1-gpl
    GITHUB_REPOSITORY OHF-Voice/piper1-gpl
    GIT_TAG main
    SOURCE_SUBDIR libpiper
    EXCLUDE_FROM_ALL YES
)

set(PIPER_ESPEAK_DATA_DIR "${piper1-gpl_BINARY_DIR}/ei/share/espeak-ng-data" CACHE PATH "")

# --------------------------------------------------------------------------
# miniaudio (header-only)
# --------------------------------------------------------------------------
CPMAddPackage(
    NAME miniaudio
    GITHUB_REPOSITORY mackron/miniaudio
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if(miniaudio_ADDED)
    add_library(miniaudio INTERFACE)
    target_include_directories(miniaudio INTERFACE "${miniaudio_SOURCE_DIR}")
endif()

# --------------------------------------------------------------------------
# libunicode (grapheme segmentation, display width, Unicode properties)
# --------------------------------------------------------------------------
find_package(libunicode QUIET)
if(NOT libunicode_FOUND)
    CPMAddPackage(
        NAME libunicode
        GITHUB_REPOSITORY contour-terminal/libunicode
        GIT_TAG v0.7.0
        OPTIONS
            "LIBUNICODE_TESTING OFF"
            "LIBUNICODE_TOOLS OFF"
    )
endif()

# --------------------------------------------------------------------------
# Subdirectories
# --------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/llm)
add_subdirectory(src/audio)
add_subdirectory(src/mcp)
add_subdirectory(src/agent)
add_subdirectory(src/tui)
add_subdirectory(src/mychat)

enable_testing()
add_subdirectory(src/tests)
