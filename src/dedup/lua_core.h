#ifndef LUA_CORE_H
#define LUA_CORE_H

#include <map>
#include <set>
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
    // How many insertions we accept before changing the step.
    unsigned int _change_step_after = 0;
    // New step after `_change_step_after` insertions
    unsigned _new_step = 0;

    void dump();
    void validate();
    FIFOData duplicate();
};

struct ThreadData {
    std::set<int> _inputs;
    std::set<int> _outputs;
    // For Deduplicate layer, because there are outputs to Compress and to 
    // Reorder.
    std::set<int> _extras;

    void push_input(int fifo_id);
    void push_output(int fifo_id);
    void push_extra(int fifo_id);
};

struct LayerData {
    std::vector<ThreadData> _thread_data;
    void push(ThreadData const& data);
    unsigned int get_total_threads() const;
};

class DedupData {
public:
    std::string _input_filename;
    std::string _output_filename;
    Compressions _compression = GZIP;
    bool _preloading = false;
    FIFOReconfigure _algorithm;
    bool _debug_timestamps = false;

    unsigned long long run_orig();
    unsigned long long run_mutex();
    unsigned long long run_smart();
    void push_layer_data(Layers layer, LayerData const& data);
    void dump(); 
    void validate();

    unsigned int get_total_threads() const;

    unsigned int push_fifo(FIFOData const& data);

    std::map<Layers, LayerData> _layers_data;
    std::map<unsigned int, FIFOData> _fifo_data;
private:
    void process_timestamp_data(std::vector<Globals::SmartFIFOTSV> const& data);
    unsigned int _fifo_id;
};

#endif // LUA_CORE_H
