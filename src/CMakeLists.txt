add_executable (sync functions.cpp sync.cpp utils.cpp config.h logging.cpp defines.cpp increase.cpp dynamic_defines.h active_promise.cpp promise_plus.cpp)

if (NOT SPDLOG_HEADER_ONLY)
    target_link_libraries (sync "${SPDLOG_LIBRARY}")
endif (NOT SPDLOG_HEADER_ONLY)

target_link_libraries (sync)
