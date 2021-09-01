#ifndef LUA_CORE_H
#define LUA_CORE_H

#include <map>
#include <string>

#include "dedupdef.h"

enum Layers {
    FRAGMENT,
    REFINE,
    DEDUPLICATE,
    COMPRESS,
    REORDER
};

enum Compressions {
    GZIP = COMPRESS_GZIP,
    BZIP = COMPRESS_BZIP2,
    NONE = COMPRESS_NONE
};

enum class FIFORole;
enum class FIFOReconfigure;

struct FIFOData {
    unsigned int _min = 1;
    unsigned int _n = 1;
    unsigned int _max = 1;
    unsigned int _with_work_threshold = 1;
    unsigned int _no_work_threshold = 1;
    unsigned int _critical_threshold = 1;
    float _increase_mult = 1.f;
    float _decrease_mult = 1.f;
    unsigned int _history_size = 10;
    bool _reconfigure = true;

    void dump() {
        std::cout << "\t\t\tFIFOData at " << this << std::endl << 
                    "\t\t\t\tmin = " << _min << std::endl <<
                    "\t\t\t\tn = " << _n << std::endl <<
                    "\t\t\t\tmax = " << _max << std::endl <<
                    "\t\t\t\tincrease = " << _increase_mult << std::endl <<
                    "\t\t\t\tdecrease = " << _decrease_mult << std::endl <<
                    "\t\t\t\thistory_size = " << _history_size << std::endl <<
                    "\t\t\t\treconfigure = " << _reconfigure << std::endl;
    }
};

struct DedupData {
    DedupData() {
        _input_filename = new std::string();
        _output_filename = new std::string();
        _fifo_data = new std::map<Layers, std::map<Layers, std::map<FIFORole, FIFOData>>>();
    }

    ~DedupData() {
        delete _input_filename;
        delete _output_filename;
        delete _fifo_data;
    }

    std::string* _input_filename = nullptr;
    std::string* _output_filename = nullptr;
    unsigned int _nb_threads = 1;
    /// On layer A, contain data for the FIFOs to layers B, C...
    std::map<Layers, std::map<Layers, std::map<FIFORole, FIFOData>>>* _fifo_data = nullptr;
    Compressions _compression = GZIP;
    bool _preloading = false;
    FIFOReconfigure _algorithm;
    bool _debug_timestamps = false;

    void push_fifo_data(Layers source, Layers destination, FIFORole role, FIFOData const& data) {
        (*_fifo_data)[source][destination][role] = data;
    }

    void set_input_filename(std::string const& filename) {
        *_input_filename = filename;
    }

    void set_output_filename(std::string const& filename) {
        *_output_filename = filename;
    }

    std::string get_input_filename() const {
        return *_input_filename;
    }

    std::string get_output_filename() const {
        return *_output_filename;
    }

    void dump() {
        std::cout << "DedupData at " << this << std::endl <<
                    "\t_input_filename = " << *_input_filename << std::endl << 
                    "\t_output_filename = " << *_output_filename << std::endl << 
                    "\t_nb_threads = " << _nb_threads << std::endl <<
                    "\t_compression = " << (int)_compression << std::endl <<
                    "\t_preloading = " << _preloading << std::endl <<
                    "\t_algorithm = " << (int)_algorithm << std::endl << 
                    "\t_debug_timestamps = " << _debug_timestamps << std::endl <<
                    "\t_fifos = " << std::endl;

        for (auto& p1: *_fifo_data) {
            for (auto& p2: p1.second) {
                for (auto& p3: p2.second) {
                    std::cout << "\t\t" << (int)p1.first << " => " << (int)p2.first << " [" << (int)p3.first << "]: " << std::endl;
                    p3.second.dump();
                }
            }
        }
    }
};

#endif // LUA_CORE_H
