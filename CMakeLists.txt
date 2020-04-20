cmake_minimum_required (VERSION 3.0.0)

list (APPEND CMAKE_MODULE_PATH
  ${CMAKE_SOURCE_DIR}/cmake
)

find_package (OpenMP REQUIRED)
find_package (Threads REQUIRED)
find_package (spdlog REQUIRED)
find_package (Boost REQUIRED)

set (CMAKE_BUILD_TYPE "Debug")
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -Wall -Wextra -Wno-sign-compare ${OpenMP_CXX_FLAGS} ${CMAKE_THREAD_LIBS_INIT}")
add_definitions ("${CMAKE_ADDITIONAL_DEFINITIONS}")

add_custom_command(OUTPUT config.h COMMAND python3 config_generator.py DEPENDS config.ini)
add_custom_command(OUTPUT dynamic_defines.h COMMAND python3 generate_dynamic_defines.py)

include_directories ("${SPDLOG_INCLUDE_DIR}" "${Boost_INCLUDE_DIRS}")
add_executable (sync functions.cpp sync.cpp utils.cpp config.h logging.cpp defines.cpp increase.cpp dynamic_defines.h)
if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (sync "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)

target_link_libraries (sync)
