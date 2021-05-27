find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-sign-compare ${CMAKE_THREAD_LIBS_INIT}")
add_definitions("-DENABLE_GZIP_COMPRESSION -DENABLE_PTHREADS -DENABLE_BZIP2_COMPRESSION")
file (GLOB DEDUP_SRC *.c)
list (REMOVE_ITEM DEDUP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/step.c")
message (STATUS "${DEDUP_SRC}")

add_library(dedup_step SHARED "step.c")
add_executable(dedup "${DEDUP_SRC}")
target_link_libraries(dedup "${OPENSSL_LIBRARIES}" "${ZLIB_LIBRARIES}" "${BZIP2_LIBRARIES}" m dedup_step)
