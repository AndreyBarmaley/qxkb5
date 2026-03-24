cmake_minimum_required(VERSION 3.16)

project(qxkb5 VERSION 20260320.1)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -O0 -Wall -Werror -Wno-sign-compare -Wno-unused-function -Wno-unused-variable")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Wall -Wno-sign-compare -Wno-unused-function -Wno-unused-variable")

include(FindPkgConfig)
set(CMAKE_FIND_FRAMEWORK LAST)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Multimedia)

set(PROJECT_SOURCES
        main.cpp mainsettings.cpp resources.qrc)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(qxkb5 MANUAL_FINALIZATION ${PROJECT_SOURCES})
else()
    add_executable(qxkb5 ${PROJECT_SOURCES})
endif()

pkg_search_module(XCB REQUIRED xcb)
pkg_search_module(XCB_XKB REQUIRED xcb-xkb)
pkg_search_module(XKBCOMMON_X11 REQUIRED xkbcommon-x11)

target_compile_options(qxkb5 PUBLIC ${XCB_CFLAGS})
target_compile_options(qxkb5 PUBLIC ${XCB_XKB_CFLAGS})
target_compile_options(qxkb5 PUBLIC ${XKBCOMMON_X11_CFLAGS})

target_link_libraries(qxkb5 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Multimedia)
target_link_libraries(qxkb5 PRIVATE ${XCB_LIBRARIES} ${XCB_XKB_LIBRARIES} ${XKBCOMMON_X11_LIBRARIES})

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.qxkb5)
endif()

set_target_properties(qxkb5 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set_target_properties(qxkb5 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

include(GNUInstallDirs)
install(TARGETS qxkb5 BUNDLE DESTINATION . LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(qxkb5)
endif()
