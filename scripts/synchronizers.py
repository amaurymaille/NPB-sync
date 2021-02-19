#!/usr/bin/python3

from collections import namedtuple

class Synchronizer:
    def __init__(self, cli_name, internal_name, str_name):
        self._cli = cli_name
        self._internal = internal_name
        self._str = str_name

class Synchronizers:
    sequential = Synchronizer("--sequential", "sequential", "Sequential")
    alt_bit = Synchronizer("--alt-bit", "alt_bit", "AltBit")
    counter = Synchronizer("--counter", "counter", "AtomicCounter")
    array_of_promises = Synchronizer("--array-of-promises", "array_of_promises", "ArrayOfPromises")
    promise_of_array = Synchronizer("--promise-of-array", "promise_of_array", "PromiseOfArray")
    naive_promise = Synchronizer("--naive-promise", "naive_promise", "NaivePromiseArray")
    # static_step = Synchronizer("--"StaticStep+"
    dsp_prod_only = Synchronizer("--dsp-prod-only", "dsp_prod_only", "DSPProdOnly")
    dsp_cons_only = Synchronizer("--dsp-cons-only", "dsp_cons_only", "DSPConsOnly")
    dsp_both = Synchronizer("--dsp-both", "dsp_both", "DSPBoth")
    dsp_prod_unblocks = Synchronizer("--dsp-prod-unblocks", "dsp_prod_unblocks", "DSPProdUnblocks")
    dsp_cons_unblocks = Synchronizer("--dsp-cons-unblocks", "dsp_cons_unblocks", "DSPConsUnblocks")
    dsp_both_unblocks = Synchronizer("--dsp-both-unblocks", "dsp_both_unblocks", "DSPBothUnblocks")
    dsp_prod_timer = Synchronizer("--dsp-prod-timer", "dsp_prod_timer", "DSPProdTimer")
    dsp_prod_timer_unblocks = Synchronizer("--dsp-prod-timer-unblocks", "dsp_prod_timer_unblocks", "DSPProdTimerUnblocks")
    dsp_monitor = Synchronizer("--dsp-monitor", "dsp_monitor", "DSPMonitor")
    dsp_never = Synchronizer("--dsp-never", "dsp_never", "DSPNever")

def dsp_modes():
    return [
        Synchronizers.dsp_prod_only._internal,
        Synchronizers.dsp_cons_only._internal,
        Synchronizers.dsp_both._internal,
        Synchronizers.dsp_prod_unblocks._internal,
        Synchronizers.dsp_cons_unblocks._internal,
        Synchronizers.dsp_both_unblocks._internal,
        Synchronizers.dsp_prod_timer._internal,
        Synchronizers.dsp_prod_timer_unblocks._internal,
        Synchronizers.dsp_monitor._internal,
        Synchronizers.dsp_never._internal
    ]
