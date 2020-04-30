cmake_minimum_required (VERSION 3.0.0)

message (${CMAKE_SOURCE_DIR})

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

add_custom_command(OUTPUT "src/config.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/config_generator.py -f ${CMAKE_SOURCE_DIR}/src/config.h -s "${CMAKE_SOURCE_DIR}/config.ini" DEPENDS "${CMAKE_SOURCE_DIR}/config.ini")
add_custom_command(OUTPUT "src/dynamic_defines.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/generate_dynamic_defines.py -f "${CMAKE_SOURCE_DIR}/src/dynamic_defines.h")

include_directories ("${SPDLOG_INCLUDE_DIR}" "${Boost_INCLUDE_DIRS}" lib/ src/)
#add_subdirectory (src)

add_executable (sync src/functions.cpp src/sync.cpp src/utils.cpp src/config.h src/logging.cpp src/defines.cpp src/increase.cpp src/dynamic_defines.h src/active_promise.cpp src/promise_plus.cpp)

if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (sync "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)

target_link_libraries (sync)
