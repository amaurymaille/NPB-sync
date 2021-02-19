#!/usr/bin/python3

import argparse
import json
import kernel_base as kb
import os
import os.path
import subprocess
import sys

"""
Generate the JSON files that contain the data for the starting matrix and the 
resulting matrix of a kernel. Can generate data for multiple kernels and 
multiple kernel configurations at once.
"""

class MatrixData:
    def __init__(self, start_filename, compute_filename):
        self._start_filename = start_filename
        self._compute_filename = compute_filename

    def generate_matrix(self):
        raise NotImplementedError()

    def run(self, data, executable, parameters_filename):
        if not os.path.isdir("../data"):
            os.mkdir("../data")

        with open(parameters_filename, "w") as f:
            json.dump(data, f)

        return subprocess.Popen([executable, "--start-matrix-file", self._start_filename, "--compute-matrix-file", self._compute_filename, "--parameters-file", parameters_filename], stdout=sys.stdout, stderr=sys.stderr)

class HeatCPUMatrixData(MatrixData):
    def __init__(self, w, x, y, z, start_filename, compute_filename):
        super(HeatCPUMatrixData, self).__init__(start_filename, compute_filename)
        self._w = w
        self._x = x
        self._y = y
        self._z = z

    def generate_matrix(self):
        w, x, y, z = self._w, self._x, self._y, self._z

        data = {
            "w": w,
            "x": x,
            "y": y,
            "z": z,
            "nb_elements": w * x * y *z
        }

        parameters_filename = "../data/heat_cpu_matrix_{}_{}_{}_{}".format(w, x, y, z)
        subprocess.Popen(["make", "-j", "8", "heat_cpu_matrix_generator"]).wait()
        return self.run(data, "./src/heat_cpu/heat_cpu_matrix_generator", parameters_filename)

    @staticmethod
    def init_from_json(data):
        return HeatCPUMatrixData(data["w"], data["x"], data["y"], data["z"], data["start"], data["compute"])

class LUMatrixData(MatrixData):
    def __init__(self, dim, start_filename, compute_filename):
        super(LUMatrixData, self).__init__(start_filename, compute_filename)
        self._dim = dim

    def generate_matrix(self):
        dim = self._dim

        data = {
            "dim": dim,
            "nb_elements": dim * dim
        }

        parameters_filename = "../data/lu_matrix_{}_{}".format(dim, dim)
        subprocess.Popen(["make", "-j", "8", "lu_matrix_generator"]).wait()
        return self.run(data, "./src/lu/lu_matrix_generator",  parameters_filename)

    @staticmethod
    def init_from_json(data):
        return LUMatrixData(data["dim"], data["start"], data["compute"])

def parse_args():
    parser = argparse.ArgumentParser(description="Generate matrix data files")
    parser.add_argument("-f", "--file", type=argparse.FileType("r"), required=False, help="Path to the file that contains the definitions of the matrices to generate. If not provided, script will process script data instead.")
    return parser.parse_args()

def generate_matrices(matrices_data):
    subprocesses = []

    if not os.path.isdir("../build"):
        os.mkdir("../build")

    os.chdir("../build")
    subprocess.Popen(["cmake", ".."]).wait()

    for matrix_data in matrices_data:
        subprocesses.append(matrix_data.generate_matrix())

    for process in subprocesses:
        process.wait()

def generate_heat_cpu_matrix_data(w, x, y, z):
    return HeatCPUMatrixData(w, x, y, z, "../data/heat_cpu_start_{}_{}_{}_{}.data".format(w, x, y, z), "../data/heat_cpu_compute_{}_{}_{}_{}.data".format(w, x, y, z))

def generate_lu_matrix_data(dim):
    return LUMatrixData(dim, "../data/lu_start_{}.data".format(dim), "../data/lu_compute_{}.data".format(dim))

def parse_file(f):
    content = json.load(f)
    data = []

    for matrix_data in content["data"]:
        kernel_type = matrix_data["type"]
        if kernel_type == kb.Kernels.HEAT_CPU.value._internal:
            data.append(HeatCPUMatrixData.init_from_json(matrix_data))
        elif kernel_type == kb.Kernels.LU.value._internal:
            data.append(LUMatrixData.init_from_json(matrix_data))
        else:
            print("Unknown kernel type {}".format(kernel_type), file=sys.stderr)

    return data

def main():
    args = parse_args()

    if args.file is not None:
        data = parse_file(args.file)
    else:
        data = [
            generate_heat_cpu_matrix_data(101, 126, 80001, 2),
        ]

    generate_matrices(data)

if __name__ == "__main__":
    main()
