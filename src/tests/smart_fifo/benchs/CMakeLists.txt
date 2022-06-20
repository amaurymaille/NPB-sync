include_directories ("${LUA_INCLUDE_DIR}")

add_executable (no_pipeline no_pipeline.cpp)
add_executable (pipeline pipeline.cpp)
target_link_libraries (no_pipeline dl core "${LUA_LIBRARIES}" "${Boost_PROGRAM_OPTIONS_LIBRARY}" nlohmann_json::nlohmann_json)
target_link_libraries (pipeline dl core "${LUA_LIBRARIES}" "${Boost_PROGRAM_OPTIONS_LIBRARY}" nlohmann_json::nlohmann_json "${}" )
