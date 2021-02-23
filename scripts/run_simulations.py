#!/usr/bin/python3

import argparse
import datetime
import json
from kernel_base import Kernels
import os
import shlex
import socket
import subprocess
import time

here = None

class Run:
    def __init__(self, description, iterations, threads, simulation_file, synchronizers, kernel_specific_synchronizers=[], generate_initial_files=False, initial_generation_file=None, start_file=None, compute_file=None):
        self._description = description
        self._iterations = iterations
        self._threads = threads
        self._simulation_file = simulation_file
        self._synchronizers = synchronizers
        self._kernel_synchronizers = kernel_specific_synchronizers
        self._start_file = start_file
        self._compute_file = compute_file
        self._generate_initial_files = generate_initial_files

        if generate_initial_files: 
            if initial_generation_file is None:
                raise RuntimeError("Cannot ask generation of initial files without providing generation parameters!")

            if start_file is None:
                raise RuntimeError("Cannot ask generation of initial files without providing start file name!")
            if compute_file is None:
                raise RuntimeError("Cannot ask generation of initial files without providing compute file name!")

        self._initial_generation_file = initial_generation_file

    def get_kernel_name(self):
        raise NotImplementedError()

    def run(self, args):
        if self._generate_initial_files:
            if self.generate_initial_files() != 0:
                print ("Error while generating initial files")
                return 1

        if self.generate_simulation_data() != 0:
            print ("Error while generating simulation data")
            return 1

        if self.build(args) != 0:
            print ("Error while building")
            return 1

        self.launch()

    def get_initial_generation_file_content(self):
        raise NotImplementedError()

    def generate_initial_files(self):
        with open(self._initial_generation_file, "w") as f:
            content = self.get_initial_generation_file_content()
            content["start"] = self._start_file
            content["compute"] = self._compute_file
            content["type"] = self.get_kernel_name()
            json.dump({ "data": [content] }, f)

        command = ["python3", "generate_matrices.py", "--file", self._initial_generation_file]
        try:
            return subprocess.Popen(command).wait()
        except:
            raise

    def generate_simulation_data(self):
        command = ["python3", "generate_simulation_data.py", "--iterations", str(self._iterations), "--description", self._description, "--file", self._simulation_file] + self._synchronizers + [self.get_kernel_name()] + self._kernel_synchronizers
        try:
            return subprocess.Popen(command).wait()
        except:
            raise

    def generate_dynamic_defines(self):
        command = self.generate_dynamic_defines_cmd()
        try:
            return subprocess.Popen(command).wait()
        except:
            raise

    def generate_dynamic_defines_cmd(self):
        NotImplementedError()

    def get_make_rule_name(self):
        raise NotImplementedError()

    def get_exec_name(self):
        raise NotImplementedError()

    def build(self, args):
        if self.generate_dynamic_defines() != 0:
            print ("Error while generating dynamic defines")
            return 1

        if not os.path.isdir("../build"):
            os.mkdir("../build")
        os.chdir("../build")

        cmake_command = generate_cmake_command(args)
        """Don't check the return code of CMake because there is a weird error
           during generation that doesn't prevent generation but still sends an
           error code at exit. Thanks CMake...
        """
        subprocess.Popen(cmake_command).wait()

        make_command = ["make", "-j", "8", self.get_make_rule_name()]
        if subprocess.Popen(make_command).wait() != 0:
            print ("Error while running make command: {}".format(" ".join(make_command)))
            return 1

        return 0

    def launch(self):
        os.putenv("OMP_NUM_THREADS", str(self._threads))
        
        hostname = socket.gethostname()
        now = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
        dirname = os.path.expanduser("~/logs/{}.{}".format(now, hostname))
        os.mkdir(dirname)

        log_filename = os.path.expanduser("{}/data.json".format(dirname))
        iterations_filename = os.path.expanduser("{}/iterations.json".format(dirname))
        runs_filename = os.path.expanduser("{}/runs.json".format(dirname))

        command = [self.get_exec_name(), "--general-log-file", log_filename, "--runs-times-file", runs_filename, "--iterations-times-file", iterations_filename, "--simulations-file", self._simulation_file, "--description", self._description]

        if self._start_file:
            command += ["--start-matrix-file", os.path.abspath(self._start_file)]

        if self._compute_file:
            command += ["--input-matrix-file", os.path.abspath(self._compute_file)]

        subprocess.Popen(command).wait()

class HeatCPURun(Run):
    def __init__(self, w, x, y, z, **kwargs):
        self._w = w
        self._x = x
        self._y = y
        self._z = z
        super(HeatCPURun, self).__init__(**kwargs)

    @staticmethod
    def generate_run(w, x, y, z, **kwargs):
        return HeatCPURun(w, x, y, z, **kwargs)

    @staticmethod
    def generate_from_json(content):
        w, x, y, z = content["w"], content["x"], content["y"], content["z"]
        del content["w"]
        del content["x"]
        del content["y"]
        del content["z"]

        return HeatCPURun.generate_run(w, x, y, z, **content)

    def get_initial_generation_file_content(self):
        return {
            "start": self._start_file,
            "compute": self._compute_file,
            "w": self._w,
            "x": self._x,
            "y": self._y,
            "z": self._z
        }


    def get_kernel_name(self):
        return "heat_cpu"

    def get_make_rule_name(self):
        return "heat_cpu"

    def get_exec_name(self):
        return "./src/heat_cpu/heat_cpu"

    def generate_dynamic_defines_cmd(self):
        command = ["python3", "generate_dynamic_defines.py", "-f", "../src/heat_cpu/dynamic_defines.h", Kernels.HEAT_CPU.value._internal, "--dimw", str(self._w), "--dimx", str(self._x), "--dimy", str(self._y), "--dimz", str(self._z)]
        return command

class LURun(Run):
    def __init__(self, dim, nb_vectors, **kwargs):
        self._dim = dim
        self._nb_vectors = nb_vectors
        super(LURun, self).__init__(**kwargs)

    @staticmethod
    def generate_run(dim, nb_vectors, **kwargs):
        return LURun(dim, nb_vectors, **kwargs)

    @staticmethod
    def generate_from_json(content):
        dim = content["dim"]
        del content["dim"]

        nb_vectors = content["nb_vectors"]
        del content["nb_vectors"]

        return LURun.generate_run(dim, nb_vectors, **content)

    def get_initial_generation_file_content(self):
        return {
            "start": self._start_file,
            "compute": self._compute_file,
            "dim": self._dim,
            "nb_vectors": self._nb_vectors
        }

    def get_kernel_name(self):
        return "lu"

    def get_make_rule_name(self):
        return "lu"

    def get_exec_name(self):
        return "./src/lu/lu"

    def generate_dynamic_defines_cmd(self):
        command = ["python3", "generate_dynamic_defines.py", "-f", "../src/lu/dynamic_defines.h", Kernels.LU.value._internal, "--dim", str(self._dim)]
        return command

def is_path(p):
    if os.path.exists(p):
        return p
    else:
        raise ValueError("File {} does not exist!".format(p))

def parse_args():
    parser = argparse.ArgumentParser(description="Launch simulations found in the parameter file")
    parser.add_argument("-d", "--debug", help="Debug build", action="store_true")
    parser.add_argument("--promise-plus-iteration-timer", help="Enable timers on PromisePlusSynchronizer iterations", action="store_true")
    parser.add_argument("--promise-plus-debug-counters", help="Enable debug counters in PromisePlus", action="store_true")
    parser.add_argument("--spdlog-include", help="spdlog include directory", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/include"))
    parser.add_argument("--spdlog-lib", help="spdlog library file", type=is_path, default=os.path.expanduser("~/NPB-sync/spdlog/build/libspdlog.a"))
    parser.add_argument("-f", "--file", help="File containing the simulations datas", type=argparse.FileType("r"), required=True)
    
    return parser.parse_args()

def generate_cmake_command(args):
    cmake_command = ["cmake", "-DCMAKE_ADDITIONAL_DEFINITIONS="]
    additional_definitions = []

    if args.promise_plus_iteration_timer:
        additional_definitions.append("-DPROMISE_PLUS_ITERATION_TIMER")

    if args.promise_plus_debug_counters:
        additional_definitions.append("-DPROMISE_PLUS_DEBUG_COUNTERS")

    cmake_command[-1] += " ".join(additional_definitions)
    cmake_command += ["-DCMAKE_BUILD_TYPE=" + ("Debug" if args.debug else "Release")]

    cmake_command += ["-DSPDLOG_INCLUDE_DIR={}".format(args.spdlog_include), "-DSPDLOG_LIBRARY={}".format(args.spdlog_lib), ".."]

    return cmake_command


def generate_runs(args):
    content = json.load(args.file)
    runs = []

    for simulation in content["simulations"]:
        kernel = simulation["kernel"]
        del simulation["kernel"]
        if kernel == Kernels.HEAT_CPU.value._internal:
            runs.append(HeatCPURun.generate_from_json(simulation))
        elif kernel == Kernels.LU.value._internal:
            runs.append(LURun.generate_from_json(simulation))
        else:
            print ("Unknown kernel type {}".format(kernel), file=sys.stderr)

    return runs

def main():
    global here
    here = os.getcwd()

    args = parse_args()
    runs = generate_runs(args)

    for run in runs:
        os.chdir(here)
        run.run(args)

if __name__ == "__main__":
    main()
