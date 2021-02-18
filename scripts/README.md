# Scripts directory

This directory contains all the scripts that are used to generate matrices for
simulations, launch the simulations and analyze the results of the simulations.

## Generating a configuration file

A configuration file is a C++ header file that exposes a singleton class that
contains attributes relating to the configuration. The script `config_generator.py`
will take an .ini file as input and output the corresponding C++ header file.

## Generating a dynamic defines file

A dynamic defines file is a C++ header file that exposes constants in a
namespace. These constants should relate to a given kernel, for example the 
dynamic defines file for the Heat CPU Kernel contains four `size_t` values
in the `HeatCPU` namespace that describe the size of the work matrix.

Dynamic defines file are useful to allow the compiler to know the bounds of 
`for` loops and perform optimisation over them.

The `generate_dynamic_defines.py` script will take as parameters the name of
kernel to configure and the associated parameters, and will output the content
of resulting the dynamic defines.

## Generating the starting matrix file and the compute matrix file

These two files are text files that contain the matrices related to a given
configuration of a kernel. The starting matrix file contains the data of the
input matrix of the kernel, the compute matrix file contains the data expected
to be output by the kernel. This is useful to run the same kernel with the same
configuration multiple times, and it avoids the initial cost of initializing the
matrix and computing its expected output with a sequential run of the kernel.

The `generate_matrices.py` script may take as parameter a JSON file describing 
the matrices to be generated. This JSON file should contain a single object
with a single field, `data`, whose type is a list of objects. Each of these
sub-objects must define the `type` field, of type `String`, that contains the
name of the kernel this sub-object represents. Kernel names are taken from 
`kernel_base.py`. Other fields of the subobjects may vary depending on the 
requested kernel. One should refer to the `init_from_json` static methods in
the `*MatrixData` classes inside the script for more information on the 
required fields.

If no JSON file is passed as parameter, the script will read a list of 
hard-coded matrices to generate. Users are allowed to change this list
if they prefer its usage to a JSON file.

The `generate_matrices.py` script makes the following assumptions:
* There is a directory `../build` relative to the position of the script ; this
directory contains the files and directories created by a **successful** call
to `cmake <path-to-root-cmakefiles.txt>` ;
* The script assumes there is a `../data` directory relative to the position  of 
the script. Some files will be written in this directory.

## Generating the configuration file for runs of a kernel

This configuration file is used by each kernel application to determine which
synchronisers will be used, as well as how many iterations will be performed
(i.e., how many times the kernel will be run).

The `generate_simulation_data.py` script will take as parameter the name of the
kernel to run, then number of iterations, the synchronisers to use, and the name
of the output file.

## Launching a run

To launch a run, use the `launch_test.py`. This script takes as parameter the name
of the kernel to run, the number of OpenMP threads to use, as well as parameters 
related to compilation (such as a debug build, whether to enable extra timing 
operations and so on). One can also specify the basename of the log directory
in which the kernel will log execution information.

## Launching several runs

To launch several runs, write a script that calls to `launch_test.py` with several
arguments.

## Analyzing runs
