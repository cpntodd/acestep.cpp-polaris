# app/CMakeLists.txt — Polaris Studio Qt 6 desktop application

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 Widgets Network Sql Multimedia)

qt_add_executable(polaris-studio
    main.cpp
    engine-process.cpp
    engine-client.cpp
    database.cpp
    library.cpp
    model-downloader.cpp
    image-engine-process.cpp
    app-settings.cpp
    system-monitor.cpp
    resources.qrc
)

target_link_libraries(polaris-studio PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Multimedia
)

set_target_properties(polaris-studio PROPERTIES
    OUTPUT_NAME "polaris-studio"
)

if(TARGET polaris-sd-cli)
    add_dependencies(polaris-studio polaris-sd-cli)
endif()

if(TARGET polaris-engine)
    add_dependencies(polaris-studio polaris-engine)
endif()
