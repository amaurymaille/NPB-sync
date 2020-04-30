#!/usr/bin/python3

import argparse
import functools

def positive_integer(helper, value):
    value_as_int = int(value)

    if value_as_int <= 0:
        raise ValueError("{} should be a strictly positive integer".format(helper))

    return int(value)

def parse_arguments():
    parser = argparse.ArgumentParser(description="Generate a dynamic_defines.h file with the given parameters")

    parser.add_argument("-w", "--dimw", help="Size of the problem (W dimension)", type=functools.partial(positive_integer, "W dimension"), default=8)
    parser.add_argument("-x", "--dimx", help="Size of the problem (X dimension)", type=functools.partial(positive_integer, "X dimension"), default=25)
    parser.add_argument("-y", "--dimy", help="Size of the problem (Y dimension)", type=functools.partial(positive_integer, "Y dimension"), default=30)
    parser.add_argument("-z", "--dimz", help="Size of the problem (Z dimension)", type=functools.partial(positive_integer, "Z dimension"), default=27)
    parser.add_argument("-l", "--loops", help="Number of global loops", type=functools.partial(positive_integer, "Global loops"), default=100000)
    
    return parser.parse_args()

def main():
    args = parse_arguments()

    with open("dynamic_defines.h", "w") as f:
        f.write("""
#ifndef DYNAMIC_DEFINES_H
#define DYNAMIC_DEFINES_H

/*
 * Let there be a hypercube H of dimensions DIM_W * DIM_X * DIM_Y * DIM_Z.
 * 
 * We iterate over DIM_W, producing DIM_W cubes C1, C2, ... C(DIM_W), each of 
 * dimensions DIM_X * DIM_Y * DIM_Z.
 * 
 * Let vectors I, J and K identify the 3D grid created by one cube. Let I be oriented
 * from left to right, J be oriented from front to back, and K be oriented from
 * bottom to top.
 * 
 * Let the I axis identify the axis created by vector I for one cube, the J axis 
 * identify the axis created by vector J and the K axis identify the axis created 
 * by vector K.
 * 
 * Let "the I dimension" refer to the DIM_X of one cube, "the J dimension" refer 
 * to the DIM_Y of one cube and "the K dimension" refer to the DIM_Z of one cube.
 * 
 * OpenMP will segment the computation on the I axis. For one cube C, OpenMP will
 * create N sub-cubes SubC1, ..., SubCN. Cube M has dimensions OMP_DIMX(M) * DIM_Y *
 * DIM_Z, with OMP_DIMX(M) a function from [O..N[ to [0..N[, representing the segmentation of 
 * a static for schedule in OpenMP. If N = 8 and DIM_X = 8, OMP_DIMX(M) = 1 for every 
 * M. If N = 8 and DIM_X = 9, OMP_DIMX(0) = 2 and OMP_DIMX(M) = 1 for every other M.
 */

namespace Globals {{
    static const size_t DIM_W = {};
    static const size_t DIM_X = {};
    static const size_t DIM_Y = {};
    static const size_t DIM_Z = {};
    static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

    static const size_t ITERATIONS = DIM_W;
    // How many points to SEND on the junction between two adjacent faces of two
    // separate sub-cubes. This properly ignores the value J = 0 as we don't
    // compute anything at this J value.
    static const size_t NB_POINTS_PER_ITERATION = (DIM_Y - 1) * DIM_Z;
    // How many points on one (JK) face of a sub-cube.
    static const size_t NB_VALUES_PER_BLOCK = DIM_Y * DIM_Z;

    // How many lines parallel to the J axis on the (JK) face of a sub-cube
    static const size_t NB_J_LINES_PER_ITERATION = DIM_Z;
    // How many lines parallel to the K axis on the (JK) face of a sub-cube
    static const size_t NB_K_LINES_PER_ITERATION = DIM_Y;

    // How many loops in the main program
    static const size_t NB_GLOBAL_LOOPS = {};
}}

#endif // DYNAMIC_DEFINES_H
""".format(args.dimw, args.dimx, args.dimy, args.dimz, args.loops))
    
    print("""Using these parameters:
    DIM_W = {}
    DIM_X = {}
    DIM_Y = {}
    DIM_Z = {}
    NB_GLOBAL_LOOPS = {}
""".format(args.dimw, args.dimx, args.dimy, args.dimz, args.loops))

if __name__ == "__main__":
    main()
