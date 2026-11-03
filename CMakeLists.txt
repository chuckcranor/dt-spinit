#
# CMakeLists.txt  cmake build for dt-spinit microbenchmark
# 27-Oct-2026  chuck@ece.cmu.edu
#

#
# set CMAKE_INSTALL_PREFIX to specify an installation directory.
# set CMAKE_PREFIX_PATH as a search path for additional external packages.
#
cmake_minimum_required(VERSION 3.10)
project (dt-spinit C)

#
# packages we always want to look for, typically required
#
find_package(dthread REQUIRED)

# globally enable -Wall if we are using clang or gcc
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall)
endif()

# avoid having to set LD_LIBRARY_PATH for non-standard install dirs
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_executable(dt-spinit app.c dt-spinit.c util.c)
target_link_libraries(dt-spinit dthread)

# "make install" rule
install (TARGETS dt-spinit RUNTIME DESTINATION bin)
