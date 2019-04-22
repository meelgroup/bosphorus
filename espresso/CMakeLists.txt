include_directories(${bosphorus_SOURCE_DIR}/espresso/main)
include_directories(${bosphorus_SOURCE_DIR}/espresso/util)
include_directories(${bosphorus_SOURCE_DIR}/espresso/mincov)
include_directories(${bosphorus_SOURCE_DIR}/espresso/sparse)

add_library(espresso OBJECT
main/cofactor.c
main/compl.c
main/contain.c
main/cubestr.c
main/cvrin.c
main/cvrm.c
main/cvrmisc.c
main/cvrout.c
main/equiv.c
main/espresso.c
main/essen.c
main/exact.c
main/expand.c
main/gasp.c
main/globals.c
main/irred.c
main/map.c
main/opo.c
main/pair.c
main/primes.c
main/reduce.c
main/set.c
main/setc.c
main/sharp.c
main/sminterf.c
main/sparse.c
main/unate.c
main/verify.c

util/cpu_time.c
util/cpu_stats.c
util/safe_mem.c
util/strsav.c
util/texpand.c
util/ptime.c
util/prtime.c
util/pipefork.c
util/pathsearch.c
util/stub.c
util/tmpfile.c

mincov/mincov.c
mincov/solution.c
mincov/gimpel.c
mincov/indep.c
mincov/dominate.c
mincov/part.c

sparse/rows.c
sparse/cols.c
sparse/matrix.c

main/main.c
)

# install(TARGETS espresso
#     EXPORT ${BOSPHORUS_EXPORT_NAME}
#     LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
#     ARCHIVE DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
#     PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include/espresso"
# )
