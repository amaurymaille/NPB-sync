import sys

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

def main():
    data = None

    if len(sys.argv) == 1:
        data = sys.stdin.readlines()
    else:
        with open(sys.argv[1]) as f:
            data = f.readlines()

    groups = group_results(data)
    results = process_groups(groups)

    for synchro in results:
        print ("{} =>".format(synchro))
        for fn in results[synchro]:
            values = results[synchro][fn]
            print ("\t{} => {}, {}".format(fn, values[0], values[1]))

if __name__ == "__main__":
   main()
