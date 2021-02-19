import argparse
import enum
import functools

class Kernel:
    def __init__(self, internal_name, str_name):
        self._internal = internal_name
        self._name = str_name

class Kernels(enum.Enum):
    HEAT_CPU = Kernel("heat_cpu", "Heat CPU")
    LU = Kernel("lu", "LU")

class KernelParserBase:
    def __init__(self, description):
        self._parser = argparse.ArgumentParser(description=description)
        subparsers = self._parser.add_subparsers()
        self._subparsers = {}
        self._callbacks = {}
        
        for kernel in Kernels:
            subparser = subparsers.add_parser(kernel.value._internal)
            self._subparsers[kernel.name] = subparser
            self._callbacks[kernel.name] = []
            subparser.set_defaults(func=functools.partial(self.run_callbacks_for_kernel, kernel.name))

    def run_callbacks_for_kernel(self, kernel, args):
        print ("Running callbacks for kernel " + str(kernel))

        for callback in self._callbacks[kernel]:
            callback(args)

    def get_parser(self):
        return self._parser

    def get_subparser_for_kernel(self, kernel):
        return self._subparsers[kernel.name]

    def add_callback_for_kernel(self, kernel, callback):
        self._callbacks[kernel.name].append(callback)

    def parse(self):
        args = self._parser.parse_args()

        if "func" not in args:
            raise RuntimeError("Kernel type required")
        args.func(args)
