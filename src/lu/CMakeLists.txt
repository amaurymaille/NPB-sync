add_library(lu_core STATIC lu.cpp matrix_core.cpp)
target_link_libraries(lu_core core)

add_executable(lu main.cpp)
target_link_libraries(lu lu_core)

add_executable(lu_matrix_generator matrix_generator.cpp)
target_link_libraries(lu_matrix_generator lu_core)
