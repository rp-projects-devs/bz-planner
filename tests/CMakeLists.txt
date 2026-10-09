FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3)
FetchContent_MakeAvailable(Catch2)

add_executable(bz_tests
    test_util.cpp
    test_catalog.cpp
    test_calc.cpp)

target_compile_definitions(bz_tests
    PRIVATE
        BZ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(bz_tests
    PRIVATE
        bz::core
        bz::warnings
        Catch2::Catch2WithMain)

if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(bz_tests)
