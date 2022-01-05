include_directories ("${LUA_INCLUDE_DIR}")

add_executable (no_pipeline no_pipeline.cpp)
target_link_libraries (no_pipeline dl core "${LUA_LIBRARIES}" "${Boost_PROGRAM_OPTIONS_LIBRARY}")
