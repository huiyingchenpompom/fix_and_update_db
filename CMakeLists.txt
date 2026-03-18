cmake_minimum_required(VERSION 3.16)

project(UiDatabase
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Qt ────────────────────────────────────────────────────────────────────────
find_package(Qt6 COMPONENTS Core Sql Network Concurrent QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 COMPONENTS Core Sql Network Concurrent REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

# ── Library target ────────────────────────────────────────────────────────────
add_library(UiDatabase SHARED
    CCUiDbModel.h
    CCQDbClientInterface.h
    CCQDbClientInterface.cpp
    cc_database_define.h
)

target_compile_definitions(UiDatabase PRIVATE UI_DATABASE_LIBRARY)

target_include_directories(UiDatabase
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(UiDatabase
        PUBLIC
            Qt6::Core
            Qt6::Sql
            Qt6::Network
            Qt6::Concurrent
    )
else()
    target_link_libraries(UiDatabase
        PUBLIC
            Qt5::Core
            Qt5::Sql
            Qt5::Network
            Qt5::Concurrent
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS UiDatabase
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES
    cc_database_define.h
    CCUiDbModel.h
    CCQDbClientInterface.h
    DESTINATION include/UiDatabase
)
