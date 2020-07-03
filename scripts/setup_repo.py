#!/usr/bin/python3

import os
import os.path
import subprocess
import sys

def download_libs():
    # Assume we are in lib directory
    here = os.getcwd()

    subprocess.Popen(["git", "clone", "https://github.com/gabime/spdlog.git"]).wait()
    os.chdir("spdlog")
    os.mkdir("build")
    os.chdir("build")
    subprocess.Popen(["cmake", ".."]).wait()
    subprocess.Popen(["make", "-j", "8"]).wait()
    
    os.chdir(here)
    
    subprocess.Popen(["git", "clone", "https://github.com/nlohmann/json.git"]).wait()
    os.chdir("json")
    os.mkdir("build")
    os.chdir("build")
    subprocess.Popen(["cmake", ".."]).wait()

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
