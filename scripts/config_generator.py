#!/usr/bin/python3

import argparse
import configparser as cp
import sys

def isint(value):
    try:
        int(value)
        return True
    except:
        return False

def isfloat(value):
    try:
        float(value)
        return True
    except:
        return False

def main():
    parser = argparse.ArgumentParser(description="Generate the content of a configuration file")
    parser.add_argument("-f", "--file", help="File to which the configuration will be written. Configuration won't be printed on screen", type=argparse.FileType("w"))
    parser.add_argument("-s", "--source", help="Source INI file from which to generate the configuration", type=argparse.FileType("r"), required=True)
    args = parser.parse_args()

    output_file = sys.stdout
    if args.file is not None:
        output_file = args.file

    # with open("config.h", "w") as f:
    print("#ifndef CONFIG_H", file=output_file)
    print("#define CONFIG_H", file=output_file)
    print("", file=output_file)

    print("#include <iostream>", file=output_file)
    print("#include <string>", file=output_file)
    print("", file=output_file)

    print("#include \"inih/INIReader.h\"", file=output_file)
    print("", file=output_file)

    print("class Config {", file=output_file)
    print("private:", file=output_file)

    class Types:
        INT = 0,
        FLOAT = 1,
        BOOL = 2,
        STRING = 3

    methods = []
    all_elements = []

    config = cp.ConfigParser()
    config.read(args.source.name)
    for section in config.sections():
        section_data = config[section]
        for key in section_data:
            value = section_data[key]
            if value == "false" or value == "0" or value == "true" or value == "1":
                print("\tbool _{};".format(key), file=output_file)
                methods.append("bool {}() {{ return _{}; }}".format(key, key))
                all_elements.append((section, key, Types.BOOL))
            elif isint(value):
                print("\tint _{};".format(key), file=output_file)
                methods.append("int {}() {{ return _{}; }}".format(key, key))
                all_elements.append((section, key, Types.INT))
            elif isfloat(value):
                print("\tfloat _{};".format(key), file=output_file)
                methods.append("float {}() {{ return _{}; }}".format(key, key))
                all_elements.append((section, key, Types.FLOAT))
            else:
                print("\tstd::string _{};".format(key), file=output_file)
                methods.append("std::string {}() {{ return _{}; }}".format(key, key))
                all_elements.append((section, key, Types.STRING))

    print("\tINIReader _reader;", file=output_file)
    print("", file=output_file)

    print("\tConfig() : _reader(\"{}\") {{".format(args.source.name), file=output_file)
    print("\t\tif (_reader.ParseError() < 0) {{\n\t\t\tstd::cerr << \"Error while parsing file {}\" << std::endl;\n\t\t}}".format(args.source.name), file=output_file)

    for section, key, elem_type in all_elements:
        fn = None
        default = None

        if elem_type == Types.INT:
            fn = "GetInteger"
            default = -1
        elif elem_type == Types.FLOAT:
            fn = "GetFloat"
            default = -1
        elif elem_type == Types.BOOL:
            fn = "GetBoolean"
            default = "false"
        else:
            fn = "Get"
            default = "\"\""

        print("\t\t_{} = _reader.{}(\"{}\", \"{}\", {});".format(key, fn, section, key, default), file=output_file)

    print("\t}", file=output_file)

    print("", file=output_file)
    print("public:", file=output_file)
    for method in methods:
        print("\t{}\n".format(method), file=output_file)

    print("\tstatic Config& instance() {\n\t\tstatic Config instance;\n\t\treturn instance;\n\t}", file=output_file)

    print("};", file=output_file)
    
    print("", file=output_file)
    
    print("#define sConfig Config::instance()", file=output_file)

    print("#endif /* CONFIG_H */", file=output_file)

if __name__ == "__main__":
    main()
