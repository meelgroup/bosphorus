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

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/GitSHA1.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp" @ONLY)
set(bosphorus_public_headers "")

set(bosph_lib_files
    anf.cpp
    cnf.cpp
    karnaugh.cpp
    replacer.cpp
    dimacscache.cpp
    extendedlinearization.cpp
    elimlin.cpp
    anfutils.cpp
    library.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp
)

if (SAT_SIMP)
set(bosph_lib_files
    ${bosph_lib_files} simplifybysat.cpp)
endif()

add_library(libbosphorus
    ${bosph_lib_files}
)

target_link_libraries(libbosphorus
  ${M4RI_LIBRARIES}
  ${BRiAl_LIBRARIES}
)

install(TARGETS libbosphorus
    EXPORT ${BOSPHORUS_EXPORT_NAME}
    LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include/bosphorus"
)

###############

add_executable(bosphorus
    main.cpp
)
set_target_properties(bosphorus PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set_target_properties(bosphorus PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

if (STATICCOMPILE)
    SET_TARGET_PROPERTIES(bosphorus PROPERTIES LINK_SEARCH_START_STATIC 1)
endif()

target_link_libraries(bosphorus
    libbosphorus
    MV
    ${Boost_LIBRARIES}
    ${PNG_LIBRARIES}
    ${M4RI_LIBRARIES}
    ${BRiAl_LIBRARIES}
    ${CRYPTOMINISAT5_LIBRARIES}
)
install(TARGETS bosphorus
    EXPORT ${BOSPHORUS_EXPORT_NAME}
    RUNTIME DESTINATION bin
)

SET(CPACK_PACKAGE_EXECUTABLES "bosphorus")

