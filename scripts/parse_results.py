#!/usr/bin/python3

import argparse
import math
import sys

import numpy as np

def group_results(data):
    times_by_fns_by_syncs = {}
    for line in data:
        _, synchro, fn, time  = line.replace("\n", "").split(" ")
        if not synchro in times_by_fns_by_syncs:
            times_by_fns_by_syncs[synchro] = {}

        times_by_fns = times_by_fns_by_syncs[synchro]
        if not fn in times_by_fns:
            times_by_fns[fn] = []

        times = times_by_fns[fn]
        seconds, nanoseconds = time.split(".")
        times += [float(seconds) + (int(nanoseconds) / 1000000000)]

    return times_by_fns_by_syncs

def avg(values):
    return sum(values) / len(values)

def var(values, average=None):
    if average is None:
        average = avg(values)

    s = 0
    for value in values:
        s += ((average - value) ** 2)

    return s / len(values)

def compute_avg_var(values):
    average = avg(values)
    variance = var(values, average)
    sigma = math.sqrt(variance)

    return average, variance, sigma

def process_groups(groups):
    results = {}

    for synchro in groups:
        for fn in groups[synchro]:
            times = groups[synchro][fn]
            average, variance, sigma = compute_avg_var(times)

            if not synchro in results:
                results[synchro] = {}

            results[synchro][fn] = (average, variance, sigma)

    return results

def print_results_raw(results):
    for synchro in results:
        print ("{} =>".format(synchro))
        for fn in results[synchro]:
            values = results[synchro][fn]
            print ("\t{} => {}, {}".format(fn, values[0], values[1]))

def compute_efficiency_compared_to(synchro, fn, data):
    target_avg = data[synchro][fn][0]
    results = {}

    for _synchro in data:
        fns = data[_synchro]
        for _fn in fns:
            if _synchro == synchro and _fn == fn:
                continue
            
            if not _synchro in results:
                results[_synchro] = {}

            results[_synchro][_fn] = target_avg / data[_synchro][_fn][0]

    return results

def compute_efficiency(data):
    results = {}

    for synchro in data:
        fns = data[synchro]
        for fn in fns:
            if not synchro in results:
                results[synchro] = {}

            results[synchro][fn] = compute_efficiency_compared_to(synchro, fn, data)

    return results

def print_efficiencies(efficiencies):
    for synchro in efficiencies:
        fns = efficiencies[synchro]
        for fn in fns:
            data = fns[fn]

            print ("{}, {}".format(synchro, fn))

            for _synchro in data:
                for _fn in data[_synchro]:
                    print ("\t{}, {} => {}".format(_synchro, _fn, data[_synchro][_fn]))

def compute_correlation_matrix(efficiencies):
    names = set()

    for synchro in efficiencies:
        fns = efficiencies[synchro]

        for fn in fns:
            fullname = synchro + " " + fn
            names.add(fullname)

    data = np.zeros((len(names), len(names)))
    data.fill(1.0)
    names_to_int = { n: i for n, i in zip(names, range(len(names))) }

    for synchro in efficiencies:
        fns = efficiencies[synchro]

        for fn in fns:
            fullname = synchro + " " + fn
            comps = fns[fn]

            for _synchro in comps:
                for _fn in comps[_synchro]:
                    _fullname = _synchro + " " + _fn
                    data[names_to_int[fullname]][names_to_int[_fullname]] = comps[_synchro][_fn]

    np.set_printoptions(linewidth=100)
    return names_to_int, data

def display_avg(results):
    longest_sync_len = -1
    longest_fun_len = -1
    longest_avg_len = -1
    longest_var_len = -1
    longest_sigma_len = -1

    for synchro in results:
        length = len(synchro)
        if length > longest_sync_len:
            longest_sync_len = length

        for function in results[synchro]:
            length = len(function)
            if length > longest_fun_len:
                longest_fun_len = length

            avg, var, sigma = (str(x) for x in results[synchro][function])
            length = len(avg)
            if length > longest_avg_len:
                longest_avg_len = length

            length = len(var)
            if length > longest_var_len:
                longest_var_len = length
            
            length = len(sigma)
            if length > longest_sigma_len:
                longest_sigma_len = length

    synchro = "Synchronization"
    function = "Function"
    avg = "Average"
    var = "Variance"
    sigma = "Deviation"
    ratio = "Deviation / Average"

    print (synchro, " " * (longest_sync_len - len(synchro) + 1), function, " " * (longest_fun_len - len(function) + 1), avg, " " * (longest_avg_len - len(avg) + 1), var, " " * (longest_var_len - len(var) + 1), sigma, " " * (longest_sigma_len - len(sigma) + 1), ratio, sep="")
    
    for synchro in results:
        for function in results[synchro]:
            _avg, _, _sigma = results[synchro][function]
            avg, var, sigma = (str(x) for x in results[synchro][function])

            remaining_sync_len = longest_sync_len - len(synchro)
            remaining_fun_len = longest_fun_len - len(function)
            remaining_avg_len = longest_avg_len - len(avg)
            remaining_var_len = longest_var_len - len(var)
            remaining_sigma_len = longest_sigma_len - len(sigma)

            print (synchro, " " * (remaining_sync_len + 1), function, " " * (remaining_fun_len + 1), avg, " " * (remaining_avg_len + 1), var, " " * (remaining_var_len + 1), sigma, " " * (remaining_sigma_len + 1), _sigma / _avg, sep='')

def parse_arguments():
    parser = argparse.ArgumentParser(description="Process logs produced by the sync application")

    parser.add_argument("file", type=argparse.FileType(mode="r"), help="File to process", nargs="?")
    parser.add_argument("-a", "--avg", action="store_true", help="Compute average values")
    parser.add_argument("--ratios", action="store_true", help="Compute ratios")
    
    csv_group = parser.add_mutually_exclusive_group()
    csv_group.add_argument("--csv", action="store_true", help="Display ratios and/or averages as CSV. No effect without --ratios or --avg")
    csv_group.add_argument("--csv-dst", type=argparse.FileType(mode="w"), metavar="filename", help="Write ratios and/or averages to file. No effect without --ratios or --avg")
    csv_group.add_argument("--csv-auto-rename", action="store_true", help="Output ratios and/or averages to a .csv file named input_file[:-3] + .csv. No effect without --ratios or --avg. Ignored if input is stdin.")

    result = parser.parse_args()
    
    return result

def main():
    args = parse_arguments()
    data = None

    if args.file is None:
        data = sys.stdin.readlines()
    else:
        data = args.file.readlines()
        args.file.close()

    data = filter(lambda s: not s.startswith("//"), data)
    groups = group_results(data)
    results = process_groups(groups)

    if not args.ratios and not args.avg:
        return

    if args.csv or args.csv_dst is not None or args.csv_auto_rename:
        output = None 
        if args.file is None or args.csv:
            output = sys.stdout 
        elif args.csv_dst:
            output = csv_dst
        else: # args.file is not None, not args.csv, not args.csv_dst -> args.csv_auto_rename
            output = open(args.file.name[:-4] + ".csv", "w")

        if args.avg:
            print ("Synchronization, Function, Average, Variance, Deviation, Deviation / Average", file=output)
            for synchro in results:
                for function in results[synchro]:
                    avg, var, dev = (str(x) for x in results[synchro][function])
                    ratio = str(float(dev) / float(avg))
                    print (synchro, function, avg, var, dev, ratio, sep=",", file=output)

            print ("", file=output)

        if args.ratios:
            efficiencies = compute_efficiency(results)
            names, matrix = compute_correlation_matrix(efficiencies)
            ints_to_name = { names[name]: name for name in names }

            print (",".join([""] + [ "\"{}\"".format(name.replace(" ", "\n")) for name in names ]), file=output)
            for i in range(len(names)):
                print (",".join([ints_to_name[i]] + [str(j) for j in matrix[i]]), file=output)

if __name__ == "__main__":
   main()
