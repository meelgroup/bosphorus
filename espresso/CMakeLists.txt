include_directories(${bosphorus_SOURCE_DIR}/espresso/main)
include_directories(${bosphorus_SOURCE_DIR}/espresso/util)
include_directories(${bosphorus_SOURCE_DIR}/espresso/mincov)
include_directories(${bosphorus_SOURCE_DIR}/espresso/sparse)

add_subdirectory(main)
add_subdirectory(util)
add_subdirectory(mincov)
add_subdirectory(sparse)

add_library(MV main/main.c)
target_link_libraries(MV main util mincov sparse)

add_executable(espresso_exec main/main_original.c)
target_link_libraries(espresso_exec main_original util mincov sparse)
