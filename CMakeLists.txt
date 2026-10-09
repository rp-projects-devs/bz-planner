cmake_minimum_required(VERSION 3.25)

project(bz-planner
    VERSION 0.1.0
    DESCRIPTION "Hypixel SkyBlock Bazaar craft-flip planner"
    HOMEPAGE_URL "https://github.com/<user>/bz-planner"
    LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BZ_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(BZ_ENABLE_OPENMP "Parallelize the scanner with OpenMP" ON)
option(BZ_ENABLE_SANITIZERS "Instrument with AddressSanitizer and UBSan" OFF)
option(BZ_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(BZ_FETCH_NEU "Download the NEU item repository at configure time" ON)
option(BZ_BUILD_DOCS "Add a 'docs' target (requires Doxygen)" OFF)

set(BZ_DATA_DIR "${PROJECT_SOURCE_DIR}/data" CACHE PATH "Runtime data directory (NEU repo, caches)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies (system packages first, otherwise fetched and pinned)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    SYSTEM
    FIND_PACKAGE_ARGS 3.11)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 2.4)

set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_MakeAvailable(nlohmann_json CLI11 httplib)

if(BZ_ENABLE_OPENMP)
    find_package(OpenMP REQUIRED COMPONENTS CXX)
endif()

# ---------------------------------------------------------------------------
# Shared build settings, consumed by every first-party target
# ---------------------------------------------------------------------------
add_library(bz_warnings INTERFACE)
add_library(bz::warnings ALIAS bz_warnings)
if(MSVC)
    target_compile_options(bz_warnings INTERFACE /W4 /permissive-
        $<$<BOOL:${BZ_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(bz_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor
        -Wold-style-cast -Woverloaded-virtual
        # Designated initializers intentionally omit members that have default values.
        -Wno-missing-field-initializers
        $<$<BOOL:${BZ_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(bz_sanitizers INTERFACE)
add_library(bz::sanitizers ALIAS bz_sanitizers)
if(BZ_ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(bz_sanitizers INTERFACE /fsanitize=address)
    else()
        target_compile_options(bz_sanitizers INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(bz_sanitizers INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
if(BZ_FETCH_NEU)
    include(cmake/FetchNeu.cmake)
endif()

add_subdirectory(core)
add_subdirectory(app)

if(BZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BZ_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    doxygen_add_docs(docs
        "${PROJECT_SOURCE_DIR}/README.md"
        "${PROJECT_SOURCE_DIR}/core/include"
        COMMENT "Generating API documentation")
endif()
