#ifndef LUA_CORE_H
#define LUA_CORE_H

#include <map>
#include <optional>
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

// General data regarding a FIFO, set on the shared buffer and 
// replicated on local buffers.
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

// The input, output and extra FIFOs of a thread. Internal IDs used are those 
// of the shared buffers, i.e. an input FIFO of 1 refers to a shared FIFO with
// an ID of 1.
//
// Push functions operate on shared FIFOs IDs.
struct ThreadData {
    std::map<int, FIFOData> _inputs;
    std::map<int, FIFOData> _outputs;
    // For Deduplicate layer, because there are outputs to Compress and to 
    // Reorder.
    std::map<int, FIFOData> _extras;

    void push_input(int fifo_id, FIFOData const& data);
    void push_output(int fifo_id, FIFOData const& data);
    void push_extra(int fifo_id, FIFOData const& data);
};

// The input, output and extra FIFOs on a layer. Stores the data for each thread that 
// will run on this layer.
struct LayerData {
    std::vector<ThreadData> _thread_data;
    void push(ThreadData const& data);
    unsigned int get_total_threads() const;
    unsigned int get_producing_threads(int fifo_id) const;
    unsigned int get_interacting_threads(int fifo_id) const;
};

class DedupData {
public:
    std::string _input_filename;
    std::optional<std::string> _observers;
    std::string _output_filename;
    Compressions _compression = GZIP;
    bool _preloading = false;
    FIFOReconfigure _algorithm;
    bool _debug_timestamps = false;

    unsigned long long run_orig();
    // unsigned long long run_mutex();
    // unsigned long long run_smart();
    unsigned long long run_auto();
    void run_numbers();
    void push_layer_data(Layers layer, LayerData const& data);
    void dump(); 
    void validate();

    unsigned int get_total_threads() const;
    unsigned int get_producing_threads(int fifo_id) const;
    unsigned int get_interacting_threads(int fifo_id) const;

    unsigned int new_fifo();

    inline void set_observers(std::string const& path) {
        _observers = std::make_optional(path);
    }

    // Maps each Layer to its input / output / extra FIFOs 
    std::map<Layers, LayerData> _layers_data;
    std::map<unsigned int, FIFOData> _fifo_data;
private:
    // void process_timestamp_data(std::vector<Globals::SmartFIFOTSV> const& data);
    unsigned int _fifo_id;
};

#endif // LUA_CORE_H
