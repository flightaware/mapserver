cmake_minimum_required(VERSION 2.8)
project(TCL_WRAP)
find_package(TCL)

LIST(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)

set(SRC_FILES maptclplug.c maptclplug.h)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
include_directories(${TCL_INCLUDE_PATH})
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/build)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/swiginc)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/tcl)
include_directories(${PROJECT_BINARY_DIR})

find_package(GD)
include_directories(${GD_INCLUDE_DIR})

find_path(TCL_INCLUDE_PATH tcl.h HINTS ${TCL_INC})

find_path(MAPSERVER_INCLUDE_PATH mapserver.h HINTS ${PROJECT_SOURCE_DIR})

add_library(maptclplug SHARED maptclplug.c)

find_library(MAPSERVER_LIBRARY names mapserver PATHS /usr/lib /usr/local/lib)

target_link_libraries(maptclplug ${TCL_LIBRARY} ${MAPSERVER_LIBMAPSERVER} ${MAPSERVER_LIBRARY})

#install(FILES ${mapscript_files} DESTINATION ${TCL_SITE_PACKAGES})
#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mapscript.py DESTINATION ${TCL_SITE_PACKAGES})
#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mapscript.so ${CMAKE_CURRENT_SOURCE_DIR}/pkgIndex.tcl ${CMAKE_CURRENT_SOURCE_DIR}/mapscriptsupp.tcl DESTINATION ${MAPSCRIPT_DEST})
#install(TARGETS mapscript DESTINATION ${TCL_SITE_PACKAGES})

