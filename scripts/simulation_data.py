#!/usr/bin/python3

import json

class SimulationData:
    def __init__(self, path, w, x, y, z, loops, active, threads):
        self._path = path
        self._w = w
        self._x = x
        self._y = y
        self._z = z
        self._loops = loops
        self._active = active
        self._threads = threads

    @staticmethod
    def json_deserialize_from(directory, filename):
        with open(directory + "/" + filename) as f:
            return SimulationData.json_deserialize(directory, json.load(f))

    @staticmethod
    def json_deserialize(directory, data):
        path = directory
        w = data["w"]
        x = data["x"]
        y = data["y"]
        z = data["z"]
        loops = data["iterations"]
        active = data["active"]
        threads = data["threads"]

        return SimulationData(path, w, x, y, z, loops, active, threads)

    def serialize(self, f):
        json.dump({
            "path": self._path,
            "threads": self._threads,
            "active": self._active,
            "iterations": self._loops,
            "w": self._w,
            "x": self._x,
            "y": self._y,
            "z": self._z,
        }, f)

    def serialize_to(self, filename):
        with open(filename, "w") as f:
            self.serialize(f)

    def __str__(self):
        return """Simulation:
\tData path: {}
\tThreads: {}
\tActive promise: {}
\tProblem size: {} x {} x {} x {}
\tLoops: {}
\tSynchronizations: {}
\tExtra arguments: {}""".format(self._path, self._threads, self._active, self._w, self._x, self._y, self._z, self._loops, ", ".join(self._synchronizations), ", ".join(self._extra_args))
