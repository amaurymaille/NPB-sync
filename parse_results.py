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

    return average, variance

def process_groups(groups):
    results = {}

    for synchro in groups:
        for fn in groups[synchro]:
            times = groups[synchro][fn]
            average, variance = compute_avg_var(times)

            if not synchro in results:
                results[synchro] = {}

            results[synchro][fn] = (average, variance)

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
    names_to_int = { n: i for n, i in zip(names, range(0, len(names))) }

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
    return names, data

def main():
    data = None

    if len(sys.argv) == 1:
        data = sys.stdin.readlines()
    else:
        with open(sys.argv[1]) as f:
            data = f.readlines()

    groups = group_results(data)
    results = process_groups(groups)

    efficiencies = compute_efficiency(results)
    names, matrix = compute_correlation_matrix(efficiencies)
    print (names)
    print (matrix)

if __name__ == "__main__":
   main()
