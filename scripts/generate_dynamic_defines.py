#!/usr/bin/python3

import argparse
import functools

def positive_integer(helper, value):
    value_as_int = int(value)

    if value_as_int <= 0:
        raise ValueError("{} should be a strictly positive integer".format(helper))

    return int(value)

def parse_arguments():
    parser = argparse.ArgumentParser(description="Generate a dynamic defines file with the given parameters")

    parser.add_argument("-w", "--dimw", help="Size of the problem (W dimension)", type=functools.partial(positive_integer, "W dimension"), default=8)
    parser.add_argument("-x", "--dimx", help="Size of the problem (X dimension)", type=functools.partial(positive_integer, "X dimension"), default=25)
    parser.add_argument("-y", "--dimy", help="Size of the problem (Y dimension)", type=functools.partial(positive_integer, "Y dimension"), default=30)
    parser.add_argument("-z", "--dimz", help="Size of the problem (Z dimension)", type=functools.partial(positive_integer, "Z dimension"), default=27)
    parser.add_argument("-f", "--file", help="File to which the configuration will be written", type=argparse.FileType("w"), required=True)
    
    return parser.parse_args()

def main():
    args = parse_arguments()

    print("""//Using these parameters:
//    DIM_W = {}
//    DIM_X = {}
//    DIM_Y = {}
//    DIM_Z = {}
""".format(args.dimw, args.dimx, args.dimy, args.dimz))

    print("""
#ifndef DYNAMIC_DEFINES_H
#define DYNAMIC_DEFINES_H

namespace Globals {{
    namespace HeatCPU {{
        static const size_t DIM_W = {};
        static const size_t DIM_X = {};
        static const size_t DIM_Y = {};
        static const size_t DIM_Z = {};
        static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

        static const size_t ITERATIONS = DIM_W;
    }}
}}

#endif // DYNAMIC_DEFINES_H
""".format(args.dimw, args.dimx, args.dimy, args.dimz), file=args.file)
    
if __name__ == "__main__":
    main()
