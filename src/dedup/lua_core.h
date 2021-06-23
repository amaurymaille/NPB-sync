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

struct FIFOData {
    unsigned int _n = 1;
    unsigned int _with_work_threshold = 1;
    unsigned int _no_work_threshold = 1;
    unsigned int _critical_threshold = 1;
    float _increase_mult = 1.f;
    float _decrease_mult = 1.f;
    unsigned int _history_size;
};

struct DedupData {
    std::string _input_filename;
    std::string _output_filename;
    unsigned int _nb_threads = 1;
    /// On layer A, contain data for the FIFOs to layers B, C...
    std::map<Layers, std::map<Layers, FIFOData>>* _fifo_data;
    Compressions _compression = GZIP;
    bool _preloading = false;
};

#endif // LUA_CORE_H
