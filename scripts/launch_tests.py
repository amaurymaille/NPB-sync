#!/usr/bin/python3

import asyncio
import datetime
import os
import json
import pprint
import shlex
import subprocess

class Run:
    class Simulation:
        def __init__(self, json_data):
            self._threads = json_data["threads"]
            self._promise_kind = json_data["promise"]
            self._dimw = json_data["dimw"]
            self._dimx = json_data["dimx"]
            self._dimy = json_data["dimy"]
            self._dimz = json_data["dimz"]
            self._loops = json_data["loops"]
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

async def perform_run(run):
    machine = run._machine

    for simulation in run._simulations:
        ssh_command = "cd NPB-sync/NPB-sync/ && if [[ -f Makefile ]]; then make clean; fi && cmake -DCMAKE_ADDITIONAL_DEFINITIONS="
        if simulation._promise_kind == "active":
            ssh_command += "-DACTIVE_PROMISES"

        ssh_command += " ."
        
        subprocess.Popen(["ssh", shlex.quote(machine), "cd NPB-sync/NPB-sync && 

def perform_runs(runs):
    for run in runs:
        asyncio.get_event_loop().create_task(perform_run(run))

    asyncio.get_event_loop().run_until_finished()

def main():
    result = json.load(open("tests.json"))
    runs = decode_to_runs(result["runs"])

    perform_runs(runs)

if __name__ == "__main__":
    main()
