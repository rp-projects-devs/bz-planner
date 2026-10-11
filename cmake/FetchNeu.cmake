# FetchNeu.cmake
#
# Downloads the NotEnoughUpdates item repository and extracts it to BZ_NEU_DIR. Every download
# deletes the recipe cache built from the previous repository (BZ_RECIPE_CACHE).
#
# Include mode (from CMakeLists.txt):
#   - downloads the repository at configure time if BZ_NEU_DIR/items is missing; `bz` then
#     rebuilds the recipe cache on its first run;
#   - defines the 'neu-update' target, which forces a fresh download, then rebuilds the recipe
#     cache with `bz neu-cache`.
#
# Script mode (used by 'neu-update'):
#   cmake -DBZ_NEU_URL=<url> -DBZ_NEU_DIR=<dir> -DBZ_RECIPE_CACHE=<file> -P FetchNeu.cmake

function(bz_download_neu url destination recipe_cache)
    set(archive "${destination}.zip")
    set(staging "${destination}.staging")

    message(STATUS "Downloading NEU repository from ${url}")
    file(DOWNLOAD "${url}" "${archive}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        file(REMOVE "${archive}")
        message(FATAL_ERROR "NEU download failed: ${reason}")
    endif()

    # Extract to a staging directory so a failed update never destroys existing data.
    file(REMOVE_RECURSE "${staging}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}")
    file(GLOB extracted LIST_DIRECTORIES true "${staging}/*")
    list(LENGTH extracted extracted_count)
    if(NOT extracted_count EQUAL 1)
        message(FATAL_ERROR "Unexpected NEU archive layout in ${staging}")
    endif()

    file(REMOVE_RECURSE "${destination}")
    file(RENAME "${extracted}" "${destination}")
    file(REMOVE_RECURSE "${staging}")
    file(REMOVE "${archive}")
    file(REMOVE "${recipe_cache}")
    message(STATUS "NEU repository ready in ${destination}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT BZ_NEU_URL OR NOT BZ_NEU_DIR OR NOT BZ_RECIPE_CACHE)
        message(FATAL_ERROR "Usage: cmake -DBZ_NEU_URL=<url> -DBZ_NEU_DIR=<dir> "
                            "-DBZ_RECIPE_CACHE=<file> -P FetchNeu.cmake")
    endif()
    bz_download_neu("${BZ_NEU_URL}" "${BZ_NEU_DIR}" "${BZ_RECIPE_CACHE}")
    return()
endif()

set(BZ_NEU_URL
    "https://github.com/NotEnoughUpdates/NotEnoughUpdates-REPO/archive/refs/heads/master.zip"
    CACHE STRING "Archive URL of the NEU item repository")
set(BZ_NEU_DIR "${BZ_DATA_DIR}/neu")
# Must match recipe_cache_file() in app/commands.cpp.
set(BZ_RECIPE_CACHE "${BZ_DATA_DIR}/cache/neu-recipes.json")

if(NOT EXISTS "${BZ_NEU_DIR}/items")
    bz_download_neu("${BZ_NEU_URL}" "${BZ_NEU_DIR}" "${BZ_RECIPE_CACHE}")
endif()

# app/CMakeLists.txt makes this target depend on `bz`, which rebuilds the cache.
add_custom_target(neu-update
    COMMAND "${CMAKE_COMMAND}"
            "-DBZ_NEU_URL=${BZ_NEU_URL}"
            "-DBZ_NEU_DIR=${BZ_NEU_DIR}"
            "-DBZ_RECIPE_CACHE=${BZ_RECIPE_CACHE}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
    COMMAND "$<TARGET_FILE:bz>" --data-dir "${BZ_DATA_DIR}" neu-cache
    COMMENT "Refreshing the NEU item repository and the recipe cache"
    VERBATIM)
