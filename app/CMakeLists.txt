add_executable(bz
    main.cpp
    commands.cpp
    display.cpp
    http.cpp)

target_compile_definitions(bz
    PRIVATE
        BZ_VERSION="${PROJECT_VERSION}"
        BZ_DEFAULT_DATA_DIR="${BZ_DATA_DIR}")

target_link_libraries(bz
    PRIVATE
        bz::core
        bz::warnings
        CLI11::CLI11
        httplib::httplib)
