# FetchNeu.cmake
#
# Downloads the NotEnoughUpdates item repository and extracts it to BZ_NEU_DIR.
#
# Include mode (from CMakeLists.txt):
#   - downloads the repository at configure time if BZ_NEU_DIR/items is missing;
#   - defines the 'neu-update' target, which forces a fresh download.
#
# Script mode (used by 'neu-update'):
#   cmake -DBZ_NEU_URL=<url> -DBZ_NEU_DIR=<dir> -P FetchNeu.cmake

function(bz_download_neu url destination)
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
    message(STATUS "NEU repository ready in ${destination}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT BZ_NEU_URL OR NOT BZ_NEU_DIR)
        message(FATAL_ERROR "Usage: cmake -DBZ_NEU_URL=<url> -DBZ_NEU_DIR=<dir> -P FetchNeu.cmake")
    endif()
    bz_download_neu("${BZ_NEU_URL}" "${BZ_NEU_DIR}")
    return()
endif()

set(BZ_NEU_URL
    "https://github.com/NotEnoughUpdates/NotEnoughUpdates-REPO/archive/refs/heads/master.zip"
    CACHE STRING "Archive URL of the NEU item repository")
set(BZ_NEU_DIR "${BZ_DATA_DIR}/neu")

if(NOT EXISTS "${BZ_NEU_DIR}/items")
    bz_download_neu("${BZ_NEU_URL}" "${BZ_NEU_DIR}")
endif()

add_custom_target(neu-update
    COMMAND "${CMAKE_COMMAND}"
            "-DBZ_NEU_URL=${BZ_NEU_URL}"
            "-DBZ_NEU_DIR=${BZ_NEU_DIR}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Refreshing the NEU item repository"
    VERBATIM)
