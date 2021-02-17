#!/usr/bin/python3

import argparse
import json

"""
Generate the JSON file containing the simulation data for a run of a kernel.
"""

def parse_args():
    parser = argparse.ArgumentParser(description="Generate a .json file containing the simulation data for a run")

    parser.add_argument("-i", "--iterations", type=int, required=True)
    #parser.add_argument("--alt-bit", help="Alternate bit synchronization", action="store_true")
    parser.add_argument("--counter", help="Atomic counter synchronization", action="store_true")
    parser.add_argument("--static-step", type=int, nargs="+")
    parser.add_argument("--array-of-promises", help="Array of promises", action="store_true")
    parser.add_argument("--promise-of-array", help="Array of promises", action="store_true")
    parser.add_argument("-f", "--file", help="Output file", type=argparse.FileType("w"))

    return parser.parse_args()

def args_to_json(args):
    data = {}
    data["iterations"] = args.iterations
    runs = []
   
    #if args.alt_bit:
    #    runs.append({"synchronizer": "alt-bit", "extras": {}})

    if args.counter:
        runs.append({"synchronizer": "counter", "extras": {}})

    if args.array_of_promises:
        runs.append({"synchronizer": "array-of-promises", "extras": {}})

    if args.static_step :
        for step in args.static_step:
            runs.append({"synchronizer": "static-step-plus", "extras": {"step": step}})

    if args.promise_of_array:
        runs.append({"synchronizer": "promise-of-array", "extras": {}})

    data["runs"] = runs
    return data

def write_to_file(f, json_data):
    if f is None:
        print (json.dumps(json_data, indent=4))
    else:
        json.dump(json_data, f, indent=4)

def process_args(args):
    as_json = args_to_json(args)
    write_to_file(args.file, as_json)
    
def main():
    args = parse_args()
    process_args(args)

if __name__ == "__main__":
    main()
