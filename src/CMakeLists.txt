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

add_subdirectory(heat_cpu)
add_subdirectory(lu)
add_subdirectory(tests)

if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (heat_cpu "${SPDLOG_LIBRARY}")
    target_link_libraries (lu "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)
