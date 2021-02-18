#!/usr/bin/python3

import argparse
import functools
import kernel_base as kb

def positive_integer(helper, value):
    value_as_int = int(value)

    if value_as_int <= 0:
        raise ValueError("{} should be a strictly positive integer".format(helper))

    return int(value)

def generate_heat_cpu_dynamic_defines(args):
    print("""//Using these parameters:
//    HeatCPU::DIM_W = {}
//    HeatCPU::DIM_X = {}
//    HeatCPU::DIM_Y = {}
//    HeatCPU::DIM_Z = {}
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

def generate_lu_dynamic_defines(args):
    print("""// Using these parameters:
// LU::DIM = {}
""".format(args.dim))

    print("""
#ifndef DYNAMIC_DEFINES_H
#define DYNAMIC_DEFINES_H

namespace Globals {{
    namespace LU {{
        static constexpr const size_t DIM = {};
        static constexpr const size_t ITERATIONS = DIM;
    }}
}}

#endif // DYNAMIC_DEFINES_H
""".format(args.dim), file=args.file)

def parse_arguments():
    parser = kb.KernelParserBase("Generate a dynamic defines file with the given parameters")
    heat_cpu = parser.get_subparser_for_kernel(kb.Kernels.HEAT_CPU)
    parser.add_callback_for_kernel(kb.Kernels.HEAT_CPU, generate_heat_cpu_dynamic_defines)
    lu = parser.get_subparser_for_kernel(kb.Kernels.LU) 
    parser.add_callback_for_kernel(kb.Kernels.LU, generate_lu_dynamic_defines)

    heat_cpu.add_argument("-w", "--dimw", help="Size of the problem (W dimension)", type=functools.partial(positive_integer, "W dimension"), default=8)
    heat_cpu.add_argument("-x", "--dimx", help="Size of the problem (X dimension)", type=functools.partial(positive_integer, "X dimension"), default=25)
    heat_cpu.add_argument("-y", "--dimy", help="Size of the problem (Y dimension)", type=functools.partial(positive_integer, "Y dimension"), default=30)
    heat_cpu.add_argument("-z", "--dimz", help="Size of the problem (Z dimension)", type=functools.partial(positive_integer, "Z dimension"), default=27)
    lu.add_argument("--dim", help="Size of the matrix", type=functools.partial(positive_integer, "Size of the matrix"), default=100)
    parser.get_parser().add_argument("-f", "--file", help="File to which the configuration will be written", type=argparse.FileType("w"), required=True)
    
    return parser.parse()

def main():
    parse_arguments()
        
if __name__ == "__main__":
    main()
