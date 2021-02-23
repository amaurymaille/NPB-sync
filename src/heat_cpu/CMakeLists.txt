add_library(heat_cpu_core STATIC heat_cpu.cpp matrix_core.cpp)
target_link_libraries(heat_cpu_core core)

add_executable(heat_cpu main.cpp)
target_link_libraries(heat_cpu heat_cpu_core)

add_executable(heat_cpu_matrix_generator matrix_generator.cpp)
target_link_libraries(heat_cpu_matrix_generator heat_cpu_core)

