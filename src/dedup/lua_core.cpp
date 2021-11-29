#include <fstream>
#include <iomanip>
#include <iostream>

#include "nlohmann/json.hpp"

#include "encoder.h"
#include "lua_core.h"

using json = nlohmann::json;

void FIFOData::dump() {
    std::cout << "\t\t\tFIFOData at " << this << std::endl << 
        "\t\t\t\tmin = " << _min << std::endl <<
        "\t\t\t\tn = " << _n << std::endl <<
        "\t\t\t\tmax = " << _max << std::endl <<
        "\t\t\t\tincrease = " << _increase_mult << std::endl <<
        "\t\t\t\tdecrease = " << _decrease_mult << std::endl <<
        "\t\t\t\thistory_size = " << _history_size << std::endl <<
        "\t\t\t\treconfigure = " << _reconfigure << std::endl;
}

void FIFOData::validate() {
    if (_min < 1) {
        std::cerr << "[WARN] min set to less than 1 in FIFOData " << this << ". Setting to 1." << std::endl;
        _min = 1;
    }
    
    if (_n < _min || _n > _max) {
        std::ostringstream error;
        error << "[FATAL] Start value of _n (" << _n << ") is lower than _min (" << _min << "), or greater than max (" << _max << ") in FIFOData " << this << std::endl;
        throw std::runtime_error(error.str());
    }

    if (_increase_mult < 1.f) {
        std::cerr << "[WARN] _increase_mult set to less than 1 in FIFOData " << this << ". Setting to 1.f." << std::endl;
    }

    if (_decrease_mult > 1.f || _decrease_mult <= 0.f) {
        std::cerr << "[WARN] _decrease_mult set to more than 1 / less than 0 in FIFOData " << this << ". Setting to 1.f." << std::endl;
    }
}

FIFOData FIFOData::duplicate() {
    return *this;
}

void ThreadData::push_input(int fifo_id, FIFOData const& data) {
    _inputs[fifo_id] = data;
}

void ThreadData::push_output(int fifo_id, FIFOData const& data) {
    _outputs[fifo_id] = data;
}

void ThreadData::push_extra(int fifo_id, FIFOData const& data) {
    _extras[fifo_id] = data;
}

void LayerData::push(ThreadData const& data) {
    _thread_data.push_back(data);
}

void DedupData::push_layer_data(Layers layer, LayerData const& data) {
    _layers_data[layer] = data;
}

void DedupData::dump() {
    std::cout << "DedupData at " << this << std::endl <<
                "\t_input_filename = " << _input_filename << std::endl << 
                "\t_output_filename = " << _output_filename << std::endl << 
                "\t_nb_threads = " << get_total_threads() << std::endl <<
                "\t_compression = " << (int)_compression << std::endl <<
                "\t_preloading = " << _preloading << std::endl <<
                "\t_algorithm = " << (int)_algorithm << std::endl << 
                "\t_debug_timestamps = " << _debug_timestamps << std::endl <<
                "\t_fifos = " << std::endl;

    /* for (auto& p1: _fifo_data) {
        for (auto& p2: p1.second) {
            for (auto& p3: p2.second) {
                for (auto& value: p3.second) {
                    std::cout << "\t\t" << (int)p1.first << " => " << (int)p2.first << " [" << (int)p3.first << "]: " << std::endl;
                    value.dump();
                }
            }
        }
    } */
}

void DedupData::validate() {
}

unsigned long long DedupData::run_orig() {
    validate();
    return EncodeDefault(*this);
}

/* unsigned long long DedupData::run_mutex() {
    validate();
    return EncodeMutex(*this);
} */

unsigned long long DedupData::run_smart() {
    validate();
    // auto [duration, datas] = EncodeSmart(*this);
    auto duration = EncodeSmart(*this);
    // process_timestamp_data(datas);
    return duration;
}

/* void DedupData::process_timestamp_data(std::vector<Globals::SmartFIFOTSV> const& data) {
    std::map<SmartFIFOImpl<chunk_t*>*, std::map<Globals::SteadyTP, std::tuple<SmartFIFO<chunk_t*>*, Globals::Action, size_t>>> processed_data;
    for (Globals::SmartFIFOTSV const& vec: data) {
        for (Globals::SmartFIFOTS const& vec_data: vec) {
            auto [tp, lfifo, fifo, action, nb_elements] = vec_data;
            processed_data[fifo][tp] = { lfifo, action, nb_elements };
        }
    }

    for (auto const& [fifo, data_map]: processed_data) {
        std::ofstream timestamp_stream("timestamps_" + fifo->description() + ".json", std::ios::out);
        timestamp_stream << std::setw(4);

        json json_data = json::array();
        size_t push_sum = 0, pop_sum = 0;
        for (auto const& [tp, tuple_data]: data_map) {
            auto const& [lfifo, action, nb_elements] = tuple_data;
            json element;
            std::string str_act = (action == Globals::Action::POP) ? "pop" : "push";
            element["action"] = str_act;
            element["time"] = std::chrono::duration_cast<std::chrono::nanoseconds>(tp - Globals::_start_time).count();
            element["lfifo"] = (uintptr_t)lfifo;
            element["nb_elements"] = nb_elements;
            if (str_act == "pop") {
                pop_sum += nb_elements;
                element["accumulate"] = pop_sum;
            } else {
                push_sum += nb_elements;
                element["accumulate"] = push_sum;
            }

            json_data.push_back(element);
        }

        json output;
        output["data"] = json_data;

        timestamp_stream << json_data;
    }
} */

unsigned int LayerData::get_total_threads() const {
    return _thread_data.size();
}

unsigned int DedupData::get_total_threads() const {
    unsigned int total = 0;
    for (auto const& [_, data]: _layers_data) {
        total += data.get_total_threads();
    }

    return total;
}

unsigned int DedupData::new_fifo() {
    return _fifo_id++;
}
