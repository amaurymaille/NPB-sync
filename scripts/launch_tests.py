#!/usr/bin/python3

import argparse
import asyncio
import datetime
import os
import os.path
import json
import pprint
import shlex
import socket
import subprocess
import time

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

def generate_ssh_command_for(machine, simulation):
    dirname = os.path.expanduser("~/NPB-sync") + "/{}.{}".format(machine, datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S"))
    ssh_command = "mkdir {} && cd {} && git clone git@github.com:amaurymaille/NPB-sync.git && cd NPB-sync && mkdir build && cd build && cmake -DSPDLOG_INCLUDE_DIR={} -DSPDLOG_LIBRARY={} -DCMAKE_ADDITIONAL_DEFINITIONS=".format(dirname, dirname, os.path.expanduser("~/NPB-sync/spdlog/include"), os.path.expanduser("~/NPB-sync/spdlog/build/libspdlog.a"))
    if simulation._promise_kind == "active":
        ssh_command += "-DACTIVE_PROMISES"

    ssh_command += (" .. && cd ../scripts && python3 generate_dynamic_defines.py -w {} -x {} -y {} -z {} -l {} -f ../src/dynamic_defines.h && python3 launch_test.py -d {} -t {} " + " ".join([ "--" + sync for sync in simulation._synchronizers ]) + " && cd && rm -rf {}").format(simulation._dimw, simulation._dimx, simulation._dimy, simulation._dimz, simulation._loops, dirname + "/NPB-sync/build", simulation._threads, dirname)

    return ssh_command
 
def perform_simulation(machine, simulation):
    ssh_command = generate_ssh_command_for(machine, simulation)
    process = subprocess.Popen(["ssh", machine, ssh_command], shell=False)
    return process

def perform_run(run):
    machine = run._machine
    processes = []

    for simulation in run._simulations:
        processes += [ perform_simulation(machine, simulation) ]
        time.sleep(1)

    return processes

async def perform_simulation_async(machine, simulation):
    ssh_command = generate_ssh_command_for(machine, simulation)
    process = await asyncio.create_subprocess_exec("ssh", machine, ssh_command)
    return process

async def perform_run_async(run):
    machine = run._machine

    for simulation in run._simulations:
        process = await perform_simulation_async(machine, simulation)
        await process.wait()
        time.sleep(1)

async def perform_runs_local_sequentiality(runs):
    simulations_coros = []

    for run in runs:
        simulations_coros += [ perform_run_async(run) ]

    await asyncio.gather(*simulations_coros)

def perform_runs(runs):
    all_processes = []

    for run in runs:
        all_processes += perform_run(run)

    for process in all_processes:
        process.wait()

def main():
    parser = argparse.ArgumentParser(description="Launch sync tests on a cluster of machines")
    parser.add_argument("-f", "--file", help="Path to JSON file containing the simulations infos", required=True, type=argparse.FileType("r"))
    parser.add_argument("-l", "--local-sequentiality", help="When several simulations must run on the same machine, run them sequentially instead of in parallel", action="store_true")
    args = parser.parse_args()

    result = json.load(args.file)
    runs = decode_to_runs(result["runs"])

    if args.local_sequentiality:
        asyncio.run(perform_runs_local_sequentiality(runs))
    else:
        perform_runs(runs)

if __name__ == "__main__":
    main()
