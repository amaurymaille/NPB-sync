#!/usr/bin/python3

import argparse
import functools
import json
import kernel_base as kb

"""
Generate the JSON file containing the simulation data for a run of a kernel.
"""

dsp_modes = [
    "dsp_prod_only",
    "dsp_cons_only",
    "dsp_both",
    "dsp_prod_unblocks",
    "dsp_cons_unblocks",
    "dsp_both_unblocks",
    "dps_prod_timer",
    "dsp_prod_timer_unblocks",
    "dsp_monitor",
    "dsp_never"
]

def add_dynamic_step_promise_arguments(parser):
    dsp_mode = parser.add_mutually_exclusive_group(required=True)
    dsp_mode.add_argument("--dsp-prod-only", action="store_true", help="Only producer is allowed to change step")
    dsp_mode.add_argument("--dsp-cons-only", action="store_true", help="Only consumers are allowed to change step")
    dsp_mode.add_argument("--dsp-both", action="store_true", help="Consumers and producer are allowed to change step")
    dsp_mode.add_argument("--dsp-prod-unblocks", action="store_true", help="Only producer is allowed to change step. This will unblock consumers")
    dsp_mode.add_argument("--dsp-cons-unblocks", action="store_true", help="Only consumers are allowed to change step. This will unblock other consumers")
    dsp_mode.add_argument("--dsp-both-unblocks", action="store_true", help="Consumers and producer are allowed to change step. This will unblock consumers")
    dsp_mode.add_argument("--dsp-prod-timer", action="store_true", help="Only producer is allowed to change step. This will be done by timing the calls to set")
    dsp_mode.add_argument("--dsp-prod-timer-unblocks", action="store_true", help="Only producer is allowed to change step. This will be done by timing the calls to set. This will unblock consumers")
    dsp_mode.add_argument("--dsp-monitor", action="store_true", help="External monitor is used to perform the calls to set (NYI)")
    dsp_mode.add_argument("--dsp-never", action="store_true", help="Step never changes (static step)")

def parse_args():
    kparser = kb.KernelParserBase("Generate a .json file containing the simulation data for a run")

    parser = kparser.get_parser()
    heat_cpu = kparser.get_subparser_for_kernel(kb.Kernels.HEAT_CPU)
    lu = kparser.get_subparser_for_kernel(kb.Kernels.LU)

    kparser.add_callback_for_kernel(kb.Kernels.HEAT_CPU, functools.partial(args_to_json, heat_cpu_args_to_json))
    kparser.add_callback_for_kernel(kb.Kernels.LU, functools.partial(args_to_json, lu_args_to_json))

    parser.add_argument("-i", "--iterations", type=int, required=True)
    parser.add_argument("--step", type=int, nargs="+", help="Initial steps for PromisePlus, mandatory if a PromisePlus mode is selected")
    parser.add_argument("--array-of-promises", help="Array of promises", action="store_true")
    parser.add_argument("--promise-of-array", help="Promise of array", action="store_true")
    parser.add_argument("-f", "--file", help="Output file", type=argparse.FileType("w"))
    parser.add_argument("--description", help="Description of the simulation", required=True)

    heat_cpu.add_argument("--alt-bit", help="Alternate bit synchronization", action="store_true")
    heat_cpu.add_argument("--counter", help="Atomic counter synchronization", action="store_true")

    add_dynamic_step_promise_arguments(parser)

    return kparser.parse()

def heat_cpu_args_to_json(runs, args):
    if args.alt_bit:
        runs.append({"synchronizer": "alt-bit", "extras": {}})

    if args.counter:
        runs.append({"synchronizer": "counter", "extras": {}})

def lu_args_to_json(runs, args):
    pass 

def args_to_json(extra_args_handler, args):
    data = {"iterations": args.iterations, "description": args.description}
    runs = []

    if args.array_of_promises:
        runs.append({"synchronizer": "array-of-promises", "extras": {}})

    if args.promise_of_array:
        runs.append({"synchronizer": "promise-of-array", "extras": {}})

    for dsp_mode in dsp_modes:
        if dsp_mode in args:
            if args.step is None:
                raise RuntimeError("Cannot specify a dynamic step promise mode without specifying a step !")

            if vars(args)[dsp_mode]:
                for step in args.step:
                    runs.append({"synchronizer": dsp_mode, "extras": {"step": step}})

    extra_args_handler(runs, args)
    data["runs"] = runs

    if args.file is None:
        print (json.dumps(data, indent=4))
    else:
        json.dump(data, args.file, indent=4)

def main():
    args = parse_args()

if __name__ == "__main__":
    main()
