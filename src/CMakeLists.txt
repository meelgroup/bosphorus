configure_file("${CMAKE_CURRENT_SOURCE_DIR}/GitSHA1.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp" @ONLY)
set(bosphorus_public_headers "")

add_library(libbosphorus
    anf.cpp
    cnf.cpp
    karnaugh.cpp
    satsolve.cpp
    simplifybysat.cpp
    replacer.cpp
    dimacscache.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/GitSHA1.cpp
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

