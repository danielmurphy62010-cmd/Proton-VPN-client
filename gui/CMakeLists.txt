cmake_minimum_required(VERSION 3.16)
project(pvc-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebEngineWidgets)

set(PVC_GUI_SOURCES
    src/main.cpp
    src/Common.h
    src/Widgets.h
    src/ApiClient.h
    src/ApiClient.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/StatusTab.h
    src/StatusTab.cpp
    src/ProfilesTab.h
    src/ProfilesTab.cpp
    src/PriorityTab.h
    src/PriorityTab.cpp
    src/ConfigEditor.h
    src/ConfigEditor.cpp
    src/StatsTab.h
    src/StatsTab.cpp
    src/LoginDialog.h
    src/LoginDialog.cpp
)

if(WIN32)
    # GUI subsystem: no console window. Qt6::Core pulls in Qt6::EntryPoint
    # automatically for WIN32_EXECUTABLE targets, so main() keeps working.
    add_executable(pvc-gui WIN32 ${PVC_GUI_SOURCES})
else()
    add_executable(pvc-gui ${PVC_GUI_SOURCES})
endif()

target_link_libraries(pvc-gui PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)

if(MSVC)
    target_compile_options(pvc-gui PRIVATE /utf-8)
endif()
