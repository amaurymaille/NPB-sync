#!/usr/bin/python3

import argparse
import json
import pandas

from synchronizers import Synchronizers as sync

class RunData:
    def __init__(self, synchronizer, function, times, extras):
        self._synchronizer = synchronizer
        self._function = function
        self._times = times
        self._extras = extras

    @staticmethod
    def init_from_json(json_data):
        return RunData(json_data["synchronizer"], json_data["function"], json_data["times"], json_data["extras"])

    def avg(self):
        return float(pandas.DataFrame(self._times).mean())

    def synchronizer(self):
        if self._synchronizer == sync.static_step:
            return self._synchronizer + str(self._extras["step"])
        else:
            return self._synchronizer

def parse_runs(f):
    runs = json.load(f)
    runs_data = []

    for run in runs["runs"]:
        runs_data.append(RunData.init_from_json(run))

    return runs_data

def parse_runs_from(filename):
    with open(filename) as f:
        return parse_runs(f)

def parse_args():
    parser = argparse.ArgumentParser(description="Convert the results of a run into a list of Python objects")
    parser.add_argument("-f", "--file", type=argparse.FileType("r"), required=True)
    return parser.parse_args()

def main():
    args = parse_args()
    print (parse_runs(args.file))

if __name__ == "__main__":
    main()
