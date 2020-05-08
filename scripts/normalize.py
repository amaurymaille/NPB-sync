#!/usr/bin/python3

import argparse
import csv
import shutil
import sys

def parse_args():
    parser = argparse.ArgumentParser(description="""
Normalize the disposition of CSV files organized as double entry arrays.
CSV files are expected to have the same array structure, i.e the first row
must contain the same values for all files, and set composed of the first
element of each row is supposed to be the same for all files.

Unless specified otherwise, files are not rewritten in place, but are instead
written to _normalize.csv versions.
""")

    parser.add_argument("-i", "--in-place", metavar="SUFFIX", help="Whether or not to rewrite files in place. If SUFFIX is provided, a copy of the original file with SUFFIX appended to its name will be created as backup")
    parser.add_argument("src_file", type=argparse.FileType("r"), help="The file used as reference for the structure")
    parser.add_argument("files", type=argparse.FileType("r"), nargs="+", help="The files to normalize")

    return parser.parse_args()

def csv_file_to_array(csv_file):
    array = []

    with csv_file as f:
        reader = csv.reader(f)
        for line in reader:
            array.append(line)

    return array

def process_file_src_array(src_array, work_file, in_place, in_place_suffix):
    work_array = csv_file_to_array(work_file)
    assert len(work_array) == len(src_array)

    for i in range(len(src_array)):
        assert len(src_array[i]) == len(work_array[i])

    for src_line_num, src_line in zip(range(1, len(src_array)), src_array[1:]):
        src_key = src_line[0]

        if work_array[src_line_num][0] == src_key:
            continue

        for work_line_num, work_line in zip(range(1, len(work_array)), work_array[1:]):
            if work_line[0] == src_key:
                # Swap lines
                work_array[src_line_num], work_array[work_line_num] = work_array[work_line_num], work_array[src_line_num]
                
                # Swap columns
                for i in range(len(work_array)):
                    work_array[i][src_line_num], work_array[i][work_line_num] = work_array[i][work_line_num], work_array[i][src_line_num]

                break

    for i in range(len(src_array)):
        assert (src_array[i][0] == work_array[i][0])
        assert (src_array[0][i] == work_array[0][i]) 

    target_file = None
    if in_place:
        save_name = work_file.name + in_place_suffix
        if save_name != work_file.name:
            shutil.copy(work_file.name, save_name)
        target_file = work_file.name
    else:
        target_file = work_file.name.replace(".csv", "_normalized.csv")

    with open(target_file, "w") as f:
        for line_num in range(len(work_array)):
            if line_num == 0:
                f.write(",".join(["\"{}\"".format(s) for s in work_array[line_num]]))
            else:
                f.write("\"{}\"".format(work_array[line_num][0]) + "," + ",".join(work_array[line_num][1:]))
            f.write("\n")

def process_file(src_file, work_file, in_place, in_place_suffix):
    src_array = csv_file_to_array(src_file)
    process_file_src_array(src_array, work_file, in_place, in_place_suffix)

def process_files(src_file, work_files, in_place, in_place_suffix):
    src_array = csv_file_to_array(src_file)

    for work_file in work_files:
        process_file_src_array(src_array, work_file, in_place, in_place_suffix)

def process_args(args):
    in_place = args.in_place is not None
    in_place_suffix = args.in_place

    src_file = args.src_file
    work_files = args.files

    process_files(src_file, work_files, in_place, in_place_suffix)
 
if __name__ == "__main__":
    args = parse_args()
    process_args(args)
