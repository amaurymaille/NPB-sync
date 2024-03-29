project (NPBSync)

cmake_minimum_required (VERSION 3.0.0)

list (APPEND CMAKE_MODULE_PATH
  ${CMAKE_SOURCE_DIR}/cmake
)

find_package (OpenMP REQUIRED)
find_package (Threads REQUIRED)
find_package (spdlog REQUIRED)
find_package (Boost REQUIRED COMPONENTS program_options thread)
find_package (Lua REQUIRED)

# message ("${LUA_INCLUDE_DIR}, ${LUA_LIBRARIES}")

set (CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/lib/json/build" "${CMAKE_MODULE_PATH}")
find_package (nlohmann_json REQUIRED)

cmake_policy(SET CMP0079 NEW)

if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE "Release")
endif()

set (CMAKE_CXX_STANDARD 20)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-sign-compare ${OpenMP_CXX_FLAGS} ${CMAKE_THREAD_LIBS_INIT}")
# Additional definitions that can be provided by external scripts
add_definitions ("${CMAKE_ADDITIONAL_DEFINITIONS}")
add_compile_options ("-Werror=return-type")

include_directories ("${SPDLOG_INCLUDE_DIR}" "${Boost_INCLUDE_DIRS}" lib/ lib/json/include src/)

add_subdirectory (lib/sol2)

#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

add_subdirectory (src)

#add_executable (sync src/functions.cpp src/sync.cpp src/utils.cpp src/config.h src/logging.cpp src/defines.cpp src/increase.cpp src/dynamic_defines.h src/active_promise.cpp src/promise_plus.cpp)

#if (NOT SPDLOG_HEADER_ONLY)
#    target_link_libraries (sync "${SPDLOG_LIBRARY}")
#endif (NOT SPDLOG_HEADER_ONLY)

# target_link_libraries (sync)
