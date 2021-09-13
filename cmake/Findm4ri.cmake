# - Try to find libm4ri
find_package(PkgConfig)
pkg_check_modules(PC_m4ri QUIET libm4ri)
set(m4ri_DEFINITIONS ${PC_m4ri_CFLAGS_OTHER})

MACRO(DBG_MSG _MSG)
    #    MESSAGE(STATUS "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}):\n${_MSG}")
ENDMACRO(DBG_MSG)

SET (m4ri_POSSIBLE_ROOT_DIRS
  "${m4ri_ROOT_DIR}"
  "$ENV{m4ri_ROOT_DIR}"
  "$ENV{m4ri_DIR}"
  "$ENV{m4ri_HOME}"
  /usr/local
  /usr
  )

FIND_PATH(m4ri_ROOT_DIR
  NAMES
  include/m4ri.h     # windows
  include/m4ri/m4ri.h # linux /opt/net
  PATHS ${m4ri_POSSIBLE_ROOT_DIRS}
)
DBG_MSG("m4ri_ROOT_DIR=${m4ri_ROOT_DIR}")

SET(m4ri_INCDIR_SUFFIXES
  include
  include/m4ri
  m4ri/include
  m4ri
)
DBG_MSG("m4ri_INCDIR_SUFFIXES=${m4ri_INCDIR_SUFFIXES}")

FIND_PATH(m4ri_INCLUDE_DIRS
  NAMES m4ri.h
  PATHS ${m4ri_ROOT_DIR}
  PATH_SUFFIXES ${m4ri_INCDIR_SUFFIXES}
  NO_CMAKE_SYSTEM_PATH
)
DBG_MSG("m4ri_INCLUDE_DIRS=${m4ri_INCLUDE_DIRS}")

SET(m4ri_LIBDIR_SUFFIXES
  lib
  lib/m4ri
  m4ri/lib
)
DBG_MSG("m4ri_LIBDIR_SUFFIXES=${m4ri_LIBDIR_SUFFIXES}")


find_library(m4ri_LIBRARIES
    NAMES m4ri libm4ri
    PATHS ${m4ri_ROOT_DIR}
    PATH_SUFFIXES ${m4ri_LIBDIR_SUFFIXES}
)
DBG_MSG("m4ri_LIBRARIES=${m4ri_LIBRARIES}")

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set m4ri_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(m4ri  DEFAULT_MSG
                                  m4ri_LIBRARIES m4ri_INCLUDE_DIRS)

mark_as_advanced(m4ri_INCLUDE_DIRS m4ri_LIBRARIES )
