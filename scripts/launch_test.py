#!/usr/bin/python3

import argparse
import datetime
import os
import os.path
import socket
import subprocess

def is_path(path):
    if not os.path.exists(path):
        raise RuntimeError("File {} does not exist".format(path))

    return path

def add_synchronization(parser, name, help_msg):
    parser.add_argument(name, help=help_msg, action="store_true")

def parse_args():
    parser = argparse.ArgumentParser(description="Run the program with the given parameters")
    parser.add_argument("-t", "--threads", help="Number of OpenMP threads", required=True, type=int, nargs="?", default=8)
    parser.add_argument("-d", "--directory", required=True, help="Directory in which to run the program", type=is_path)

    promise_mode = parser.add_mutually_exclusive_group(required=True)
    promise_mode.add_argument("--active", help="Use active promises", action="store_true")
    promise_mode.add_argument("--passive", help="Use passive promises", action="store_true")

    parser.add_argument("--spdlog-include", help="spdlog include directory", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/include"))
    parser.add_argument("--spdlog-lib", help="spdlog library file", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/build/libspdlog.a"))
    parser.add_argument("--increase-file", help="CPP file used for the definition of the increase functions", type=is_path)
    parser.add_argument("--args", help="Additional arguments passed to the program")

    add_synchronization(parser, "--sequential", "Run sequential program")
    add_synchronization(parser, "--alt-bit", "Run with alternate bit synchronizer")
    add_synchronization(parser, "--iteration", "Run with iteration synchronizer")
    add_synchronization(parser, "--block", "Run with block promising synchronizer")
    add_synchronization(parser, "--block-plus", "Run with block promising plus synchronizer")
    add_synchronization(parser, "--jline", "Run with jline promising synchronizer")
    add_synchronization(parser, "--jline-plus", "Run with jline promising plus synchronizer")
    add_synchronization(parser, "--increasing-jline", "Run with increasing jline promising synchronizer")
    add_synchronization(parser, "--increasing-jline-plus", "Run with increasing jline promising plus synchronizer")
    add_synchronization(parser, "--kline", "Run with kline promising synchronizer")
    add_synchronization(parser, "--kline-plus", "Run with jline promising plus synchronizer")
    add_synchronization(parser, "--increasing-kline", "Run with increasing kline promising synchronizer")
    add_synchronization(parser, "--increasing-kline-plus", "Run with increasing kline promising plus synchronizer")
    parser.add_argument("-a", "--all", help="Run with all synchronizations", action="store_true")
    
    return parser.parse_args()

def run(threads, synchronizations, directory, spdlog_include, spdlog_lib, active, increase_file, extra_args):
    synchronizations = [ "--" + sync for sync in synchronizations ]
    now = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    hostname = socket.gethostname()
    os.chdir(os.path.expanduser(directory))

    cmake_command = ["cmake"]
    if active:
        cmake_command += ["-DCMAKE_ADDITIONAL_DEFINITIONS=-DACTIVE_PROMISES"]

    cmake_command += ["-DSPDLOG_INCLUDE_DIR={}".format(spdlog_include), "-DSPDLOG_LIBRARY={}".format(spdlog_lib)]

    if increase_file:
        cmake_command += ["-DSYNC_INCREASE_FILE={}".format(increase_file)]

    cmake_command += [".."]
    
    subprocess.Popen(cmake_command).wait()
    subprocess.Popen(["make", "-j", "6"]).wait()

    os.putenv("OMP_NUM_THREADS", str(threads))

    with open(os.path.expanduser("~/logs/{}.{}.log".format(now, hostname)), "w") as log_file:
        iterations_filename = os.path.expanduser("~/logs/{}.{}.iterations.log".format(now, hostname))
        runs_filename = os.path.expanduser("~/logs/{}.{}.runs.log".format(now, hostname))

        log_file.write("// OMP_NUM_THREADS={}\n".format(threads))
        log_file.write("// Active promise: {}\n".format(active))
        log_file.write("// Synchronizations: " + " ".join(synchronizations) + "\n")
        log_file.write("// Extra args: " + extra_args + "\n")
        log_file.flush()
        cat = subprocess.Popen(["cat", "../src/dynamic_defines.h"], stdout=subprocess.PIPE)
        subprocess.run(["awk", "{print \"//\", $0 }"], stdout=log_file, stdin=cat.stdout)

        subprocess.Popen(["./src/sync"] + synchronizations + extra_args.split(" ") + ["--runs-times-file", runs_filename, "--iterations-times-file", iterations_filename]).wait()

def main():
    def add_sync_if_exists(synchronizations, args, synchronization):
        if getattr(args, synchronization):
            synchronizations.append(synchronization)

    args = parse_args()
    
    threads = args.threads
    syncs = [ "sequential", "alt_bit", "iteration", "block", "block_plus", "jline", "jline_plus", "increasing_jline", "increasing_jline_plus", "kline", "kline_plus", "increasing_kline", "increasing_kline_plus" ]
    synchronizations = []
    directory = args.directory
    
    if args.all:
        synchronizations = syncs
    else:
        for synchronization in syncs:
            add_sync_if_exists(synchronizations, args, synchronization)

    run(threads, synchronizations, directory, args.spdlog_include, args.spdlog_lib, args.active, args.increase_file, args.args or "")

if __name__ == "__main__":
    main()
