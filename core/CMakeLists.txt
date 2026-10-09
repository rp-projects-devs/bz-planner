add_library(bz_core
    src/util.cpp
    src/catalog.cpp
    src/neu.cpp
    src/bazaar.cpp
    src/pricing.cpp
    src/solver.cpp
    src/planner.cpp
    src/scanner.cpp)
add_library(bz::core ALIAS bz_core)

target_include_directories(bz_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(bz_core PUBLIC cxx_std_20)

target_link_libraries(bz_core
    PUBLIC
        bz::sanitizers
    PRIVATE
        bz::warnings
        nlohmann_json::nlohmann_json
        $<$<BOOL:${BZ_ENABLE_OPENMP}>:OpenMP::OpenMP_CXX>)
