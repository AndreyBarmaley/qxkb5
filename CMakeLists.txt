cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -O0 -Wall -Werror -Wno-sign-compare -Wno-unused-function -Wno-unused-variable")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Wall -Wno-sign-compare -Wno-unused-function -Wno-unused-variable")

include(FindPkgConfig)
set(CMAKE_FIND_FRAMEWORK LAST)

project(qxkb5 VERSION 20250305.1)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Gui Widgets Multimedia REQUIRED)

pkg_search_module(XCB REQUIRED xcb)
pkg_search_module(XCB_XKB REQUIRED xcb-xkb)
pkg_search_module(XKBCOMMON_X11 REQUIRED xkbcommon-x11)

add_executable(qxkb5 main.cpp mainsettings.cpp resources.qrc)

target_compile_options(qxkb5 PUBLIC ${XCB_CFLAGS})
target_compile_options(qxkb5 PUBLIC ${XCB_XKB_CFLAGS})
target_compile_options(qxkb5 PUBLIC ${XKBCOMMON_X11_CFLAGS})

target_link_libraries(qxkb5 Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Multimedia)
target_link_libraries(qxkb5 ${XCB_LIBRARIES} ${XCB_XKB_LIBRARIES} ${XKBCOMMON_X11_LIBRARIES})

set_target_properties(qxkb5 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
