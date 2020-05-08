#!/usr/bin/python3

import argparse
import datetime
import os
import os.path
import socket
import subprocess

def is_path(path):
    if not os.path.exists(path):
        raise RuntimeError("Directory {} does not exist".format(path))

    return path

def add_synchronization(parser, name, help_msg):
    parser.add_argument(name, help=help_msg, action="store_true")

def parse_args():
    parser = argparse.ArgumentParser(description="Run the program with the given parameters")
    parser.add_argument("-t", "--threads", help="Number of OpenMP threads", required=True, type=int, nargs="?", default=8)
    parser.add_argument("-d", "--directory", required=True, help="Directory in which to run the program", type=is_path)
    add_synchronization(parser, "--sequential", "Run sequential program")
    add_synchronization(parser, "--alt-bit", "Run with alternate bit synchronizer")
    add_synchronization(parser, "--iteration", "Run with iteration synchronizer")
    add_synchronization(parser, "--block", "Run with block promising synchronizer")
    add_synchronization(parser, "--block-plus", "Run with block promising plus synchronizer")
    add_synchronization(parser, "--jline", "Run with jline promising synchronizer")
    add_synchronization(parser, "--jline-plus", "Run with jline promising plus synchronizer")
    add_synchronization(parser, "--increasing-jline", "Run with increasing jline promising synchronizer")
    add_synchronization(parser, "--increasing-jline-plus", "Run with increasing jline promising plus synchronizer")
    parser.add_argument("-a", "--all", help="Run with all synchronizations", action="store_true")
    
    return parser.parse_args()

def run(threads, synchronizations, directory):
    synchronizations = [ "--" + sync for sync in synchronizations ]
    now = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    hostname = socket.gethostname()
    log_file = open(os.path.expanduser("~/logs/{}.{}.log".format(now, hostname)), "w")

    os.chdir(os.path.expanduser(directory))

    subprocess.Popen(["make", "-j", "6"]).wait()

    os.putenv("OMP_NUM_THREADS", str(threads))

    log_file.write("// OMP_NUM_THREADS={}\n".format(threads))
    log_file.flush()
    cat = subprocess.Popen(["cat", "../src/increase.cpp"], stdout=subprocess.PIPE)
    subprocess.run(["awk", "{print \"//\", $0 }"], stdout=log_file, stdin=cat.stdout)

    subprocess.Popen(["./src/sync"] + synchronizations, stdout=log_file).wait()

    log_file.close()

def main():
    def add_sync_if_exists(synchronizations, args, synchronization):
        if getattr(args, synchronization):
            synchronizations.append(synchronization)

    args = parse_args()
    
    threads = args.threads
    syncs = [ "sequential", "alt_bit", "iteration", "block", "block_plus", "jline", "jline_plus", "increasing_jline", "increasing_jline_plus" ]
    synchronizations = []
    directory = args.directory
    
    if args.all:
        synchronizations = syncs
    else:
        for synchronization in syncs:
            add_sync_if_exists(synchronizations, args, synchronization)

    run(threads, synchronizations, directory)

if __name__ == "__main__":
    main()
