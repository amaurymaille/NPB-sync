# add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/config.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/config_generator.py -f ${CMAKE_SOURCE_DIR}/src/config.h -s "${CMAKE_SOURCE_DIR}/config.ini" DEPENDS "${CMAKE_SOURCE_DIR}/config.ini")
# add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/dynamic_defines.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/generate_dynamic_defines.py -f "${CMAKE_SOURCE_DIR}/src/dynamic_defines.h")

file (GLOB PROMISE_FILES promises/*/*.cpp promises/*/*.tpp)
add_library(core STATIC argv.cpp core.h defines.cpp dynamic_config.cpp fifo.h 
                        logging.cpp matrix_core.h naive_promise.h 
                        promise_plus.cpp promise_plus.tpp time_log.h utils.cpp
                        utils.tpp "${PROMISE_FILES}")

target_link_libraries(core "${Boost_PROGRAM_OPTIONS_LIBRARY}"
                           "${Boost_THREAD_LIBRARY}"
                           nlohmann_json::nlohmann_json)

add_library(heat_cpu_core heat_cpu/heat_cpu.cpp heat_cpu/matrix_core.cpp)
target_link_libraries(heat_cpu_core core)

add_executable(heat_cpu heat_cpu/main.cpp)
target_link_libraries(heat_cpu heat_cpu_core)

add_executable(heat_cpu_matrix_generator heat_cpu/matrix_generator.cpp)
target_link_libraries(heat_cpu_matrix_generator heat_cpu_core)

add_executable(lu lu/lu.cpp lu/matrix_core.cpp lu/main.cpp)
target_link_libraries(lu core)

#add_executable (sync sync.cpp utils.cpp
#                ${CMAKE_CURRENT_SOURCE_DIR}/config.h logging.cpp defines.cpp
#                ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_defines.h 
#                promise_plus.cpp "${PROMISE_FILES}" argv.cpp dynamic_config.cpp 
#                matrix_core.cpp "${FUNCTIONS_FILES}")

#add_executable (matrix_generator matrix_core.cpp matrix_generator.cpp)

#target_link_libraries (sync "${Boost_PROGRAM_OPTIONS_LIBRARY}"
#                            "${Boost_THREAD_LIBRARY}"
#                            nlohmann_json::nlohmann_json)

#target_link_libraries (matrix_generator "${Boost_PROGRAM_OPTIONS_LIBRARY}"
#                                        "${Boost_THREAD_LIBRARY}"
#                                        nlohmann_json::nlohmann_json)

add_subdirectory(tests)

if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (heat_cpu "${SPDLOG_LIBRARY}")
    target_link_libraries (lu "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)
