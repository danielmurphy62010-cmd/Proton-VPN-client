cmake_minimum_required(VERSION 3.21)
project(proton_vpn_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network WebEngineCore WebEngineWidgets)

qt_add_executable(proton-vpn-gui WIN32
    main.cpp
)

target_link_libraries(proton-vpn-gui PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
)

install(TARGETS proton-vpn-gui RUNTIME DESTINATION .)
