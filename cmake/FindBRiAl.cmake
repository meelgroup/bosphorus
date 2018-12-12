# - Try to find libbrial
find_package(PkgConfig)
pkg_check_modules(PC_BRiAl QUIET libbrial libpolybori)
set(BRiAl_DEFINITIONS ${PC_BRiAl_CFLAGS_OTHER})

MACRO(DBG_MSG _MSG)
    #    MESSAGE(STATUS "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}):\n${_MSG}")
ENDMACRO(DBG_MSG)

SET (BRiAl_POSSIBLE_ROOT_DIRS
  "${BRiAl_ROOT_DIR}"
  "$ENV{BRiAl_ROOT_DIR}"
  "$ENV{BRiAl_DIR}" 
  "$ENV{BRiAl_HOME}"
  /usr/local
  /usr
  )

FIND_PATH(BRiAl_ROOT_DIR
  NAMES
  include/polybori.h     # windows
  include/polybori/polybori.h # linux /opt/net
  PATHS ${BRiAl_POSSIBLE_ROOT_DIRS}
)
DBG_MSG("BRiAl_ROOT_DIR=${BRiAl_ROOT_DIR}")

SET(BRiAl_INCDIR_SUFFIXES
  include
  include/polybori
  polybori/include
  polybri
)
DBG_MSG("BRiAl_INCDIR_SUFFIXES=${BRiAl_INCDIR_SUFFIXES}")

FIND_PATH(BRiAl_INCLUDE_DIRS
  NAMES polybori.h
  PATHS ${BRiAl_ROOT_DIR}
  PATH_SUFFIXES ${BRiAl_INCDIR_SUFFIXES}
  NO_CMAKE_SYSTEM_PATH
)
DBG_MSG("BRiAl_INCLUDE_DIRS=${BRiAl_INCLUDE_DIRS}")

SET(BRiAl_LIBDIR_SUFFIXES
  lib64
  lib
  lib/brial
  brial/lib
)
DBG_MSG("BRiAl_LIBDIR_SUFFIXES=${BRiAl_LIBDIR_SUFFIXES}")


find_library(BRiAl_LIBRARIES
    NAMES brial libbrial polybori libpolybori
    PATHS ${BRiAl_ROOT_DIR}
    PATH_SUFFIXES ${BRiAl_LIBDIR_SUFFIXES}
)
DBG_MSG("BRiAl_LIBRARIES=${BRiAl_LIBRARIES}")

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set BRiAl_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(brial  DEFAULT_MSG
                                  BRiAl_LIBRARIES BRiAl_INCLUDE_DIRS)

mark_as_advanced(BRiAl_INCLUDE_DIRS BRiAl_LIBRARIES)
