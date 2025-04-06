# Copyright (c) 2016, Security Research Labs
# Copyright (C) 2018  Mate Soos, Davin Choo, Kian Ming A. Chai, DSO National Laboratories
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

include_directories( ${PROJECT_SOURCE_DIR} )

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/GitSHA1.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp" @ONLY)
include_directories(${CRYPTOMINISAT5_INCLUDE_DIRS})

include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
set(bosph_lib_files
    anf.cpp
    cnf.cpp
    anfcnfutils.cpp
    replacer.cpp
    dimacscache.cpp
    extendedlinearization.cpp
    elimlin.cpp
    anfutils.cpp
    bosphorus.cpp
    simplifybysat.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp
)

add_library(bosphorus ${bosph_lib_files})
target_link_libraries(bosphorus
    ${m4ri_LIBRARIES}
    ${BRiAl_LIBRARIES}
    ${PNG_LIBRARIES}
    ${CRYPTOMINISAT5_LIBRARIES}
)

set_target_properties(bosphorus PROPERTIES
    PUBLIC_HEADER "${bosphorus_public_headers}"
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

add_executable(bosphorus-bin
    main.cpp
)
set_target_properties(bosphorus-bin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    INSTALL_RPATH_USE_LINK_PATH TRUE
    OUTPUT_NAME bosphorus
)

target_link_libraries(bosphorus-bin
    bosphorus
    Boost::program_options
)

bosphorus_add_public_header(bosphorus ${CMAKE_CURRENT_SOURCE_DIR}/bosphorus.hpp )

# -----------------------------------------------------------------------------
# Copy public headers into build directory include directory.
# The bosphorusConfig.cmake we generate in the build directory depends on
# this.
# -----------------------------------------------------------------------------
set(HEADER_DEST "${PROJECT_BINARY_DIR}/include/bosphorus")
add_custom_target(CopyPublicHeaders ALL)
get_target_property(bosphorus_public_headers bosphorus PUBLIC_HEADER)
foreach(public_header ${bosphorus_public_headers})
    get_filename_component(HEADER_NAME ${public_header} NAME)
    add_custom_command(TARGET CopyPublicHeaders PRE_BUILD
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                               "${HEADER_DEST}"
                       COMMAND ${CMAKE_COMMAND} -E echo
                       "Copying ${HEADER_NAME} to ${HEADER_DEST}"
                       COMMAND ${CMAKE_COMMAND} -E
                           copy_if_different
                           ${public_header}
                           "${HEADER_DEST}"
                      )
endforeach()

install(TARGETS bosphorus
    EXPORT ${BOSPHORUS_EXPORT_NAME}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bosphorus"
)

install(TARGETS bosphorus-bin
    # EXPORT ${BOSPHORUS_EXPORT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
