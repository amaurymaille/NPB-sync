add_executable (test_dynamic_step test_dynamic_step.cpp)
add_executable (test_fifo_plus test_fifo_plus.cpp)
target_link_libraries (test_dynamic_step core)
target_link_libraries (test_fifo_plus core)
