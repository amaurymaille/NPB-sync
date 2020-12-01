#!/usr/bin/python3

import json
import os
import os.path
import subprocess
import sys

class MatrixData:
    def __init__(self, w, x, y, z, start_filename, compute_filename):
        self._w = w
        self._x = x
        self._y = y
        self._z = z
        self._start_filename = start_filename
        self._compute_filename = compute_filename

def generate_matrix(matrix_data):
    w, x, y, z = matrix_data._w, matrix_data._x, matrix_data._y, matrix_data._z
    start_filename = matrix_data._start_filename
    compute_filename = matrix_data._compute_filename

    data = {
        "w": w,
        "x": x,
        "y": y,
        "z": z,
        "nb_elements": w * x * y *z
    }

    parameters_filename = "../data/matrix_{}_{}_{}_{}".format(w, x, y, z)
    with open(parameters_filename, "w") as f:
        json.dump(data, f)

    return subprocess.Popen(["./src/matrix_generator", "--start-matrix-file", start_filename, "--compute-matrix-file", compute_filename, "--parameters-file", parameters_filename], stdout=sys.stdout, stderr=sys.stderr)

def generate_matrices(matrices_data):
    subprocesses = []

    os.chdir("../build")
    subprocess.Popen(["cmake", ".."]).wait()
    subprocess.Popen(["make", "-j", "8", "matrix_generator"]).wait()

    for matrix_data in matrices_data:
        subprocesses.append(generate_matrix(matrix_data))

    for process in subprocesses:
        process.wait()

def generate_matrix_data(w, x, y, z):
    return MatrixData(w, x, y, z, "../data/start_{}_{}_{}_{}.data".format(w, x, y, z), "../data/compute_{}_{}_{}_{}.data".format(w, x, y, z))

def main():
    data = [
        generate_matrix_data(101, 126, 80001, 2),
    ]

    generate_matrices(data)

if __name__ == "__main__":
    main()
