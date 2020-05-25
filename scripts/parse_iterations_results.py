#!/usr/bin/python3

import argparse
import itertools
import matplotlib.pyplot as plt
import os.path
import pandas as pd
import seaborn

def parse_args():
    parser = argparse.ArgumentParser(description="Script that processes a CSV like file that contains the time of each iterations of the synchronization patterns")
    parser.add_argument("-f", "--file", type=argparse.FileType("r"), help="File to use", required=True)
    return parser.parse_args()

def main():
    args = parse_args()
    content = {}

    for line in itertools.islice(args.file.readlines(), 1, None):
        line = line[:-1]
        global_it, local_it, synchro, function, time = line.split(" ")
        if not synchro in content:
            content[synchro] = {}

        if not function in content[synchro]:
            content[synchro][function] = {}

        if not global_it in content[synchro][function]:
            content[synchro][function][global_it] = []

        content[synchro][function][global_it] += [float(time)]

    # print (content)
    data = []
    
    for synchro in content:
        for function in content[synchro]:
            fun_data = []
            for global_it in content[synchro][function]:
                 for i in range(len(content[synchro][function][global_it])):
                    fun_data.append({"Iteration": i, "Time": content[synchro][function][global_it][i], "courbe": function + str(global_it)})
            data.append(fun_data)
    
    count = 0
    for d in data:
        df = pd.DataFrame(d)
        print ("Argh")
        plot = seaborn.lineplot(data=df, x="Iteration", y="Time", hue="courbe")
        plt.savefig(os.path.expanduser("~/toto" + str(count)))
        plt.clf()
        count += 1

if __name__ == "__main__":
    main()
