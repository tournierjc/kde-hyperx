cmake_minimum_required(VERSION 3.20)
project(kde-hyperx VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

add_executable(kde-hyperx
    src/main.cpp
    src/HyperXDevice.cpp
    src/TrayIcon.cpp
)

target_link_libraries(kde-hyperx PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::HIDAPI
)

target_include_directories(kde-hyperx PRIVATE src)

install(TARGETS kde-hyperx DESTINATION bin)
install(FILES udev/99-hyperx-headset.rules DESTINATION /etc/udev/rules.d)
install(FILES desktop/kde-hyperx.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
install(FILES desktop/kde-hyperx-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/xdg/autostart)
