include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(fifo_plus_spsc SHARED fifo_plus_spsc.cpp)

target_link_libraries(fifo_plus_spsc core)
