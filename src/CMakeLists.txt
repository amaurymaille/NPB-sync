add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/config.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/config_generator.py -f ${CMAKE_SOURCE_DIR}/src/config.h -s "${CMAKE_SOURCE_DIR}/config.ini" DEPENDS "${CMAKE_SOURCE_DIR}/config.ini")
add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/dynamic_defines.h" COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/generate_dynamic_defines.py -f "${CMAKE_SOURCE_DIR}/src/dynamic_defines.h")

add_executable (sync functions.cpp sync.cpp utils.cpp ${CMAKE_CURRENT_SOURCE_DIR}/config.h logging.cpp defines.cpp increase.cpp ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_defines.h active_promise.cpp promise_plus.cpp)

if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (sync "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)
