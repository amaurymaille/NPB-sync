#!/usr/bin/python3

import argparse
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
            for time in run._times:
                data.append({"time": time})

            data = pandas.DataFrame(data)
            avg = data.mean()
            var = data.var()
            std = data.std()

            all_datas.append({"sync": run._synchronizer, "fun": run._function, "avg": avg, "var": var, "std": std})

        print (all_datas)

def generate_numerics_raw(simulations_datas):
    for simulation_data in simulations_datas:
        generate_numerics_raw_for(simulation_data)

def generate_numerics(simulations_datas, smart):
    if not smart: 
        generate_numerics_raw(simulations_datas)
    else:
        generate_numerics_smart(simulations_datas)

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
if __name__ == "__main__":
    main()
