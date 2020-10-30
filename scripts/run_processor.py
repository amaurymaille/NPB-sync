#!/usr/bin/python3

import argparse
import csv
import json
import pandas
import matplotlib.pyplot as plt
import numpy as np
import os
import seaborn

import run_parser
from simulation_data import SimulationData
from synchronizers import Synchronizers as sync

def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("-d", "--directories", nargs="+", help="Paths of the directories where data is stored", required=True)
    parser.add_argument("--histogram", action="store_true", help="Generate histograms representing the dispersion of time for each run of a synchronization pattern")
    parser.add_argument("--static-step-evolution", action="store_true", help="Generate visualization of the evolution of average time of runs for StaticStepPromise with different amount of lines in synchronization")
    parser.add_argument("--smart", action="store_true", help="Attempt to smartly generate graphs (combine values from different set of data when parameters match)")
    parser.add_argument("--violin", action="store_true", help="Generate violin plots representing boxplots and density of the time for each synchronization pattern in a given configuration")
    parser.add_argument("--numerics", action="store_true", help="Compute average, variance and standard deviation. Output to numerics.csv")
    parser.add_argument("--ratios", action="store_true", help="Compute ratios based on what is available. Output to ratios.txt")
    parser.add_argument("--summary", action="store_true", help="Perform a summary: recall the parameters of the problem, the synchronization tools used, and which is the best, with speedups")

    return parser.parse_args()

def process_directory(directory):
    return SimulationData.json_deserialize_from(directory, "data.json")

def process_directories(directories):
    results = []

    for directory in directories:
        results.append(process_directory(directory))

    return results

def generate_histogram_raw_for_one(run, path):
    times = run._times
    seaborn.distplot(times)
    if run._synchronizer == sync.static_step:
        name = "{}.{}.{}".format(run._synchronizer, run._function, run._extras["step"])
    else:
        name = "{}.{}".format(run._synchronizer, run._function)

    plt.savefig(os.path.expanduser(path) + "/{}.distplot.png".format(name)) 
    plt.clf()

def generate_histogram_raw_for_comp(runs, path):
    for run in runs:
        if run._synchronizer == sync.static_step:
            name = "{}.{}.{}".format(run._synchronizer, run._function, run._extras["step"])
        else:
            name = "{}.{}".format(run._synchronizer, run._function)

        

def generate_histogram_raw_forall(simulation_data):
    with open(simulation_data._path + "/runs.json") as f:
        runs = run_parser.parse_runs(f)

        for run in runs:
            generate_histogram_raw_for_one(run, simulation_data._path)

        # generate_histogram_raw_for_comp(runs, simulation_data._path)

def generate_histograms_raw(simulations_datas):
    for simulation_data in simulations_datas:
        generate_histogram_raw_forall(simulation_data)

def generate_histograms_smart(simulations_datas):
    pass

def generate_histograms(simulations_datas, smart):
    if not smart:
        generate_histograms_raw(simulations_datas)
    else:
        generate_histograms_smart(simulations_datas)

def generate_static_step_evolution_raw_for(runs, path):
    data = []

    for run in runs:
        if run._synchronizer != sync.static_step:
            continue

        avg = np.average(run._times)
        data.append({"step": run._extras["step"], 
                     "time": avg})

    fig, ax = plt.subplots()
    ax.set(xscale="log")
    seaborn.scatterplot(ax=ax, x="step", y="time", data=pandas.DataFrame(data))
    plt.savefig(os.path.expanduser(path) + "/static_step_evolution.png")
    plt.clf()


def generate_static_step_evolution_raw(simulations_datas):
    for simulation_data in simulations_datas:
        with open(simulation_data._path + "/runs.json") as f:
            runs = run_parser.parse_runs(f)
            generate_static_step_evolution_raw_for(runs, simulation_data._path)

   
def generate_static_step_evolution_smart(simulations_datas):
    pass

def generate_static_step_evolution(simulations_datas, smart):
    if not smart:
        generate_static_step_evolution_raw(simulations_datas)
    else:
        generate_static_step_evolution_smart(simulations_datas)


def generate_violins_raw_for(simulation_data):
    with open(simulation_data._path + "/runs.json") as f:
        runs = run_parser.parse_runs(f)
        data = []

        for run in runs:
            for time in run._times:
                data.append({"synchronizer": run._synchronizer,
                             "function": run._function + ("" if run._synchronizer != sync.static_step else ".{}".format(run._extras["step"])),
                             "time": time})

        df = pandas.DataFrame(data)
        seaborn.catplot(data=df, x="synchronizer", y="time", hue="function", kind="violin")
        plt.savefig(os.path.expanduser(simulation_data._path) + "/violin.png")
        plt.clf()

def generate_violins_raw(simulations_datas):
    for simulation_data in simulations_datas:
        generate_violins_raw_for(simulation_data)    

def generate_violins_smart(simulations_datas):
    pass 

def generate_violins(simulations_datas, smart):
    if not smart:
        generate_violins_raw(simulations_datas)
    else:
        generate_violins_smart(simulations_datas)


def generate_numerics_raw_for(simulation_data):
    with open(simulation_data._path + "/runs.json") as f:
        runs = run_parser.parse_runs(f)

        all_datas = []

        for run in runs:
            data = []
            if type(run._times) == type([]):
                for time in run._times:
                    data.append({"time": time})
            else:
                for time in run._times:
                    data.append({"time": time["time"]})

            data = pandas.DataFrame(data)
            avg = data.mean()
            var = data.var()
            std = data.std()

            extras = None
            if run._synchronizer == sync.static_step:
                extras = "step: {}".format(run._extras["step"])

            all_datas.append({"sync": run._synchronizer, "fun": run._function, "extras": extras, "avg": avg, "var": var, "std": std, "vc": std / avg})

    with open(os.path.expanduser(simulation_data._path) + "/numerics.csv", "w") as f:
        fieldnames = ["sync", "fun", "extras", "avg", "var", "std", "vc"]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(all_datas)

def generate_numerics_raw(simulations_datas):
    for simulation_data in simulations_datas:
        generate_numerics_raw_for(simulation_data)

def generate_numerics(simulations_datas, smart):
    if not smart: 
        generate_numerics_raw(simulations_datas)
    else:
        generate_numerics_smart(simulations_datas)

class RatioHelper:
    def __init__(self, runs):
        self._syncs = {}

        for run in runs:
            if run._synchronizer == sync.static_step:
                if not run._synchronizer in self._syncs:
                    self._syncs[sync.static_step] = {}

                self._syncs[run._synchronizer][str(run._extras["step"])] = run
            else:
                self._syncs[run._synchronizer] = run

    def has_static_step(self, step=None):
        if step is None:
            return sync.static_step in self._syncs

        return self.has_static_step() and str(step) in self._syncs[sync.static_step]

    def has_sync(self, sync):
        return sync in self._syncs

    def has_naive(self):
        return self.has_sync(sync.naive_promise)

    def has_alt_bit(self):
        return self.has_sync(sync.alt_bit)

    def select_best_static_step(self):
        if not self.has_static_step():
            return None

        times = {}
        for step in self._syncs[sync.static_step]:
            data = self._syncs[sync.static_step][step]
            times[data._extras["step"]] = data.avg()

        return self._syncs[sync.static_step][str(min(times, key=times.get))]

    def select_static_step(self, step):
        if self.has_static_step(step):
            return self._syncs[sync.static_step][str(step)]

        return None

    def select_naive(self):
        if self.has_naive():
            return self._syncs[sync.naive_promise]
        
        return None

    def select_alt_bit(self):
        if self.has_alt_bit():
            return self._syncs[sync.naive_promise]

        return None

    def select_static_steps(self):
        if self.has_static_step():
            return self._syncs[sync.static_step]

        return None

    def select_best(self):
        best = None
        time = float("+inf")

        for synchro in self._syncs:
            if synchro == sync.static_step:
                best_static_step = self.select_best_static_step()
                if best_static_step.avg() < time:
                    time = best_static_step.avg()
                    best = best_static_step
            else:
                avg = self._syncs[synchro].avg()
                if avg < time:
                    time = avg
                    best = self._syncs[synchro]

        return best, time

def generate_ratios(simulations_datas, smart):
    if not smart:
        generate_ratios_raw(simulations_datas)
    else:
        generate_ratios_smart(simulations_datas)

def generate_ratios_raw(simulations_datas):
    for simulation_data in simulations_datas:
        generate_ratios_raw_for(simulation_data)

def generate_ratios_raw_for(simulation_data):
    ratios = []

    with open(simulation_data._path + "/runs.json") as f:
        runs = run_parser.parse_runs(f)

        computer = RatioHelper(runs)
        best_static_step = computer.select_best_static_step()
        naive = computer.select_naive()
        alt_bit = computer.select_alt_bit()
        step_1 = computer.select_static_step(1)

        if naive and step_1:
            ratios.append("Naive / Promise+1: {}".format(naive.avg() / step_1.avg()))
            ratios.append("Promise+1 / Naive: {}".format(step_1.avg() / naive.avg()))

        if naive and best_static_step:
            ratios.append("Naive / Promise+{} (best): {}".format(best_static_step._extras["step"], naive.avg() / best_static_step.avg()))
            ratios.append("Promise+{} (best) / Naive: {}".format(best_static_step._extras["step"], best_static_step.avg() / naive.avg()))

    with open(simulation_data._path + "/ratios.txt", "w") as f: 
        f.write("\n".join(ratios))

def generate_summaries(simulations_datas):
    for simulation_data in simulations_datas:
        generate_summary(simulation_data)

def generate_summary(simulation_data):
    data = None

    with open(simulation_data._path + "/data.json") as f:
        data = json.load(f)

    runs = None
    with open(simulation_data._path + "/runs.json") as f:
        runs = run_parser.parse_runs(f)

    best, time = RatioHelper(runs).select_best()

    with open(simulation_data._path + "/summary.txt", "w") as f:
        f.write("{} * {} * {} * {} with {} threads (using active wait: {}) and {} iterations\n".format(data["w"], data["x"], data["y"], data["z"], data["threads"], data["active"], data["iterations"]))
        f.write("Running with the following synchronization patterns: \n")
        f.write("\t" + "\n\t".join([run.synchronizer() + ": " + str(run.avg()) for run in runs]))
        f.write("\n")
        f.write("Best synchronization: {}, with average time: {}".format(best.synchronizer(), time))

def main():
    args = parse_args()

    seaborn.set()

    simulations_datas = process_directories(args.directories)
    # print (simulations_datas)

    if args.histogram:
        generate_histograms(simulations_datas, args.smart)

    # print (simulations_datas)
    if args.static_step_evolution:
        generate_static_step_evolution(simulations_datas, args.smart)

    if args.violin:
        generate_violins(simulations_datas, args.smart)

    if args.numerics:
        generate_numerics(simulations_datas, args.smart)

    if args.ratios:
        generate_ratios(simulations_datas, args.smart)

    if args.summary:
        generate_summaries(simulations_datas)
if __name__ == "__main__":
    main()
