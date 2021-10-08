find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

include_directories ("${CMAKE_CURRENT_SOURCE_DIR}" "${LUA_INCLUDE_DIR}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-sign-compare ${CMAKE_THREAD_LIBS_INIT}")
add_definitions("-DENABLE_GZIP_COMPRESSION -DENABLE_PTHREADS -DENABLE_BZIP2_COMPRESSION")
file (GLOB DEDUP_SRC *.cpp)
list (REMOVE_ITEM DEDUP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/step.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/lua_old.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/encode_fifo.cpp")
# message (STATUS "${DEDUP_SRC}")

# add_library(dedup_step SHARED "step.cpp")
add_executable(dedup "${DEDUP_SRC}")
target_link_libraries(dedup "${OPENSSL_LIBRARIES}" "${ZLIB_LIBRARIES}" "${BZIP2_LIBRARIES}" "${LUA_LIBRARIES}" m 
#dedup_step 
core dl "${Boost_PROGRAM_OPTIONS_LIBRARY}")
