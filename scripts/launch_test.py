#!/usr/bin/python3

import argparse
import datetime
import os
import os.path
import socket
import subprocess
import sys

def is_path(path):
    if not os.path.exists(path):
        raise RuntimeError("File {} does not exist".format(path))

    return path

def parse_args():
    parser = argparse.ArgumentParser(description="Run the program with the given parameters")
    parser.add_argument("-t", "--threads", help="Number of OpenMP threads", type=int, default=8)
    parser.add_argument("-d", "--debug", help="Debug build", action="store_true")
    parser.add_argument("--promise-plus-iteration-timer", help="Enable timers on PromisePlusSynchronizer iterations", action="store_true")

    promise_mode = parser.add_mutually_exclusive_group(required=True)
    promise_mode.add_argument("--active", help="Use active promises", action="store_true")
    promise_mode.add_argument("--passive", help="Use passive promises", action="store_true")

    parser.add_argument("--spdlog-include", help="spdlog include directory", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/include"))
    parser.add_argument("--spdlog-lib", help="spdlog library file", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/build/libspdlog.a"))

    parser.add_argument("--dims", type=int, nargs=4, help="Dimensions of the problem", metavar=("W", "X", "Y", "Z"))
    parser.add_argument("-f", "--file", type=argparse.FileType("r"), help="File to use as the input for the program, should contain simulation data", required=True)
    
    return parser.parse_args()

def run(threads, spdlog_include, spdlog_lib, active, promise_plus_iteration_timer, dims, src_filename, debug):
    os.chdir(os.path.dirname(os.path.abspath(sys.argv[0])))

    if dims:
        w, x, y, z = dims
        dynamic_defines_cmd = ["python3", "generate_dynamic_defines.py", "-w", str(w), "-x", str(x), "-y", str(y), "-z", str(z), "-f", "../src/dynamic_defines.h"]
        subprocess.Popen(dynamic_defines_cmd).wait()

    now = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    hostname = socket.gethostname()
    os.chdir("../build")

    cmake_command = ["cmake", "-DCMAKE_ADDITIONAL_DEFINITIONS="]
    additional_definitions = []
    if active:
        additional_definitions.append("-DACTIVE_PROMISES")

    if promise_plus_iteration_timer:
        additional_definitions.append("-DPROMISE_PLUS_ITERATION_TIMER")

    cmake_command[-1] += " ".join(additional_definitions)

    cmake_command += ["-DCMAKE_BUILD_TYPE=" + ("Debug" if debug else "Release")]

    cmake_command += ["-DSPDLOG_INCLUDE_DIR={}".format(spdlog_include), "-DSPDLOG_LIBRARY={}".format(spdlog_lib), ".."]

    print (cmake_command)
    subprocess.Popen(cmake_command).wait()
    subprocess.Popen(["make", "-j", "8"]).wait()

    os.putenv("OMP_NUM_THREADS", str(threads))

    dirname = os.path.expanduser("~/logs/{}.{}".format(now, hostname))
    os.mkdir(dirname)

    log_filename = os.path.expanduser("{}/data.json".format(dirname))
    iterations_filename = os.path.expanduser("{}/iterations.json".format(dirname))
    runs_filename = os.path.expanduser("{}/runs.json".format(dirname))

    subprocess.Popen(["./src/sync", "--parameters-file", log_filename, "--runs-times-file", runs_filename, "--iterations-times-file", iterations_filename, "--simulations-file", src_filename]).wait()

def main():
    args = parse_args()

    threads = args.threads
    
    run(threads, args.spdlog_include, args.spdlog_lib, args.active, args.promise_plus_iteration_timer, args.dims, os.path.abspath(args.file.name), args.debug)

if __name__ == "__main__":
    main()
