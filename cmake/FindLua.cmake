find_path(LUA_INCLUDE_DIR
  NAMES
    lua.h
  PATHS
    # Do not replace. Script setup_repo.py populates this with the correct 
    # information.
    -- LUA_INCLUDE_PATH --
)

find_library (LUA_LIBRARIES
  NAMES
    liblua
  PATHS
    -- LUA_LIB_PATH --
)
