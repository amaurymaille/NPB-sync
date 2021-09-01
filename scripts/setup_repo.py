#!/usr/bin/python3

import os
import os.path
import subprocess
import sys

def download_libs():
    # Assume we are in lib directory
    here = os.getcwd()

    if not os.path.isdir("spdlog"):
        subprocess.Popen(["git", "clone", "https://github.com/gabime/spdlog.git"]).wait()
        os.chdir("spdlog")
        os.mkdir("build")
        os.chdir("build")
        subprocess.Popen(["cmake", ".."]).wait()
        subprocess.Popen(["make", "-j", "8"]).wait()
    
    os.chdir(here)
    
    if not os.path.isdir("json"):
        subprocess.Popen(["git", "clone", "https://github.com/nlohmann/json.git"]).wait()
        os.chdir("json")
        os.mkdir("build")
        os.chdir("build")
        subprocess.Popen(["cmake", ".."]).wait()

    os.chdir(here)

    """lua_base = "lua-5.4.3"
    if not os.path.isdir(lua_base):
        subprocess.Popen(["wget", "http://www.lua.org/ftp/" + lua_base + ".tar.gz"]).wait()
        subprocess.Popen(["tar", "xvzf", lua_base + ".tar.gz"]).wait()
        subprocess.Popen(["rm", lua_base + ".tar.gz"]).wait()
        os.chdir(lua_base)
        proc = subprocess.Popen(["sed", "-i.orig", "-e", 's/^INSTALL_TOP=.*/INSTALL_TOP= "' + os.getcwd().replace("/", "\/") + '\/build"/', "Makefile"])
        proc.wait()
        os.mkdir("build")
        subprocess.Popen(["make"]).wait()
        subprocess.Popen(["make", "install"]).wait()
        subprocess.Popen(["sed", "-i.orig", "-e", "s/-- LUA_INCLUDE_PATH --/\"" + os.getcwd().replace("/", "\/") + "\/src\"/", "-e", "s/-- LUA_LIB_PATH --/\"" + os.getcwd().replace("/", "\/") + "\/build\/lib\"/", here + "/../cmake/FindLua.cmake"]).wait()"""

    if not os.path.isdir("sol2"):
        subprocess.Popen(["git", "clone", "git@github.com:ThePhD/sol2.git"].wait())

def main():
    dirname = os.path.dirname(os.path.abspath(sys.argv[0]))
    print (dirname)
    cwd = os.getcwd()

    if dirname != cwd:
        print ("Current working directory is {}, moving to {}".format(cwd, dirname))
        os.chdir(dirname)

    # Move up
    os.chdir(os.path.dirname(dirname))

    # Libraries directory
    os.chdir("lib")

    download_libs()

if __name__ == "__main__":
    main()
