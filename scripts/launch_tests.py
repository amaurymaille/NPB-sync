#!/usr/bin/python3

import argparse
import datetime
import os
import os.path
import json
import pprint
import shlex
import socket
import subprocess

def assert_is_int(value):
    assert type(value) == type(0)

def assert_is_in_list(value, elements):
    assert value in elements

def assert_are_ints(*values):
    for value in values:
        assert_is_int(value)

class Run:
    class Simulation:
        def __init__(self, json_data):
            self._threads = json_data["threads"]
            assert_is_int(self._threads)

            self._promise_kind = json_data["promise"]
            assert_is_in_list(self._promise_kind, ["passive", "active"])

            self._dimw = json_data["dimw"]
            self._dimx = json_data["dimx"]
            self._dimy = json_data["dimy"]
            self._dimz = json_data["dimz"]
            self._loops = json_data["loops"]

            assert_are_ints(self._dimw, self._dimx, self._dimy, self._dimz, self._loops)

            self._synchronizers = json_data["synchronizers"]

    def __init__(self, json_data):
        self._machine = json_data["machine"]
        self._simulations = [ Run.Simulation(s) for s in json_data["simulations"] ]

    def __str__(self):
        output = "Run on machine {}\n".format(self._machine)
        for simulation in self._simulations:
            output += """\tThreads      : {}
\tPromise Kind : {}
\tDimensions   : {} * {} * {} * {}
\tLoops        : {}
\tSynchronizers: {}

""".format(simulation._threads, simulation._promise_kind, simulation._dimw, simulation._dimx, simulation._dimy, simulation._dimz, simulation._loops, ", ".join(simulation._synchronizers))

        return output

def decode_to_runs(data):
    runs = []
    for obj in data:
        runs.append(Run(obj))

    return runs

def perform_run(run):
    machine = run._machine
    processes = []

    for simulation in run._simulations:
        dirname = os.path.expanduser("~/NPB-sync") + "/{}.{}".format(socket.gethostname(), datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S"))
        ssh_command = "mkdir {} && cd {} && git clone git@github.com:amaurymaille/NPB-sync.git && mkdir build && cd build && cmake -DCMAKE_ADDITIONAL_DEFINITIONS=".format(dirname, dirname)
        if simulation._promise_kind == "active":
            ssh_command += "-DACTIVE_PROMISES"

        ssh_command += (" .. && cd ../scripts && python3 generate_dynamic_defines.py -w {} -x {} -y {} -z {} -l {} -f ../src/dynamic_defines.h && python3 launch_test.py -d {} -t {} " + " ".join([ "--" + sync for sync in simulation._synchronizers ])).format(simulation._dimw, simulation._dimx, simulation._dimy, simulation._dimz, simulation._loops, os.path.expanduser("~/NPB-sync/NPB-sync/build/src"), simulation._threads)
        
        # print ("ssh", shlex.quote(machine), shlex.quote(ssh_command))
        process = subprocess.Popen(["ssh", machine, ssh_command], shell=False)
        processes += [ process ]
        # print (ssh_command)

    return processes

def perform_runs(runs):
    all_processes = []

    for run in runs:
        all_processes += perform_run(run)

    for process in all_processes:
        process.wait()

def main():
    parser = argparse.ArgumentParser(description="Launch sync tests on a cluster of machines")
    parser.add_argument("-f", "--file", help="Path to JSON file containing the simulations infos", required=True, type=argparse.FileType("r"))
    args = parser.parse_args()

    result = json.load(args.file)
    runs = decode_to_runs(result["runs"])

    perform_runs(runs)

if __name__ == "__main__":
    main()
