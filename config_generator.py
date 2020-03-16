#!/usr/bin/python3

import configparser as cp

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
    with open("config.h", "w") as f:
        f.write("#ifndef CONFIG_H\n")
        f.write("#define CONFIG_H\n")
        f.write("\n")

        f.write("#include <string>\n")
        f.write("\n")

        f.write("#include \"inih/INIReader.h\"\n")
        f.write("\n")

        f.write("class Config {\n")
        f.write("private:\n")

        class Types:
            INT = 0,
            FLOAT = 1,
            BOOL = 2,
            STRING = 3

        methods = []
        all_elements = []

        config = cp.ConfigParser()
        config.read("config.ini")
        for section in config.sections():
            section_data = config[section]
            for key in section_data:
                value = section_data[key]
                if value == "false" or value == "0" or value == "true" or value == "1":
                    f.write("\tbool _{};\n".format(key))
                    methods.append("bool {}() {{ return _{}; }}".format(key, key))
                    all_elements.append((section, key, Types.BOOL))
                elif isint(value):
                    f.write("\tint _{};\n".format(key))
                    methods.append("int {}() {{ return _{}; }}".format(key, key))
                    all_elements.append((section, key, Types.INT))
                elif isfloat(value):
                    f.write("\tfloat _{};\n".format(key))
                    methods.append("float {}() {{ return _{}; }}".format(key, key))
                    all_elements.append((section, key, Types.FLOAT))
                else:
                    f.write("\tstd::string _{};\n".format(key))
                    methods.append("std::string {}() {{ return _{}; }}".format(key, key))
                    all_elements.append((section, key, Types.STRING))

        f.write("\tINIReader _reader;\n")
        f.write("\n")

        f.write("\tConfig() : _reader(\"config.ini\") {\n")
        f.write("\t\tif (_reader.ParseError() < 0) {\n\t\t\tstd::cerr << \"Error while parsing file config.ini\" << std::endl;\n\t\t}\n")

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

            f.write("\t\t_{} = _reader.{}(\"{}\", \"{}\", {});\n".format(key, fn, section, key, default))

        f.write("\t}\n")

        f.write("\n")
        f.write("public:\n")
        for method in methods:
            f.write("\t{}\n\n".format(method))

        f.write("\tstatic Config& instance() {\n\t\tstatic Config instance;\n\t\treturn instance;\n\t}\n")

        f.write("};\n")
        
        f.write("\n")
        
        f.write("#define sConfig Config::instance()\n")

        f.write("#endif /* CONFIG_H */\n")

if __name__ == "__main__":
    main()
