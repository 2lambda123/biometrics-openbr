# ================================================================
# The Stasm CMake configuration file
#
# Usage from an external project:
#   In your CMakeLists.txt, add these lines:
#
#   find_package(Stasm3 REQUIRED)
#   target_link_libraries(MY_TARGET ${Stasm4_LIBS})
# ================================================================

find_path(Stasm_DIR include/stasm.hpp ${CMAKE_SOURCE_DIR}/3rdparty/*)

add_subdirectory(${Stasm_DIR} ${Stasm_DIR}/build)

set(SRC ${SOURCE};${SRC})

include_directories(${Stasm_DIR}/include)
link_directories(${Stasm_DIR}/build)

set(Stasm3_LIBS stasm)
