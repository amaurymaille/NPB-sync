find_path (SPDLOG_INCLUDE_DIR
  NAMES
    spdlog/spdlog.h
  PATHS
    ${CMAKE_SOURCE_DIR}/lib/spdlog/include
    /usr/include
)

if (NOT SPDLOG_HEADER_ONLY)
  find_library (SPDLOG_LIBRARY
    NAMES
      libspdlog spdlog
    PATHS
      ${CMAKE_SOURCE_DIR}/lib/spdlog/build
  )
endif (NOT SPDLOG_HEADER_ONLY)
