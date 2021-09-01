add_executable (test_dynamic_step test_dynamic_step.cpp)
add_executable (test_fifo_plus test_fifo_plus.cpp)

add_subdirectory (fifo_plus)
add_subdirectory (smart_fifo)

include_directories ("${LUA_INCLUDE_DIR}")

target_link_libraries (test_dynamic_step core
                       "${LUA_LIBRARIES}")
target_link_libraries (test_fifo_plus core
                       "${LUA_LIBRARIES}"
                       dl)
