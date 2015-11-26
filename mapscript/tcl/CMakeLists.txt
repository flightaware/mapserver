cmake_minimum_required(VERSION 2.8)
project(TCL_WRAP)
find_package(SWIG REQUIRED)
include(${SWIG_USE_FILE})
find_package(TCL)

LIST(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${TCL_INCLUDE_PATH})
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/build)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/swiginc)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/)
include_directories(${PROJECT_SOURCE_DIR}/mapscript/tcl)

set(CMAKE_SWIG_FLAGS "")


find_package(GD)

if(NOT GD_FOUND)
#report_optional_not_found(GD)
report_required_not_found(GD)
else(NOT GD_FOUND)
include_directories(${GD_INCLUDE_DIR})
set (USE_GD 1)
if(GD_SUPPORTS_PNG)
  set(USE_GD_PNG 1)
endif(GD_SUPPORTS_PNG)
if(GD_SUPPORTS_GIF)
  set(USE_GD_GIF 1)
endif(GD_SUPPORTS_GIF)
if(GD_SUPPORTS_JPEG)
  set(USE_GD_JPEG 1)
endif(GD_SUPPORTS_JPEG)
if(GD_SUPPORTS_FREETYPE)
  set(USE_GD_FREETYPE 1)
endif(GD_SUPPORTS_FREETYPE)
endif(NOT GD_FOUND)

find_path(TCL_INCLUDE_PATH tcl.h
  HINTS ${TCL_INC}
)

find_library(MAPSERVER_LIBRARY names mapserver PATHS /usr/lib /usr/local/lib)

find_library(TCLGD_LIBRARY names tclgd PATHS /usr/local/lib/tclgd1.2)
#set(TCLGD_LIBRARY "/usr/local/lib/tclgd1.2/libtclgd.so.1")

# we want our mapscript commands in a namespace, not massively polluting
# the global namespace
set_property(SOURCE mapscript.i PROPERTY SWIG_FLAGS "-namespace")


set (MAPSCRIPT_DEST "/usr/local/lib/MapscriptTcl1.4")

SWIG_ADD_MODULE(tclmapscript tcl mapscript.i)

SWIG_LINK_LIBRARIES(tclmapscript ${TCL_LIBRARY} ${MAPSERVER_LIBMAPSERVER} ${MAPSERVER_LIBRARY} ${TCLGD_LIBRARY})

set_target_properties(${SWIG_MODULE_tclmapscript_REAL_NAME} PROPERTIES PREFIX "")
set_target_properties(${SWIG_MODULE_tclmapscript_REAL_NAME} PROPERTIES OUTPUT_NAME mapscript)

get_target_property(LOC_MAPSCRIPT_LIB ${SWIG_MODULE_tclmapscript_REAL_NAME} $<TARGET_FILE>)
set(mapscript_files ${LOC_MAPSCRIPT_LIB} ${CMAKE_CURRENT_SOURCE_DIR}/*.tcl)

#install(FILES ${mapscript_files} DESTINATION ${TCL_SITE_PACKAGES})

#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mapscript.py DESTINATION ${TCL_SITE_PACKAGES})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mapscript.so ${CMAKE_CURRENT_SOURCE_DIR}/pkgIndex.tcl ${CMAKE_CURRENT_SOURCE_DIR}/mapscriptsupp.tcl DESTINATION ${MAPSCRIPT_DEST})
#install(TARGETS mapscript DESTINATION ${TCL_SITE_PACKAGES})

