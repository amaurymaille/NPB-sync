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

void DedupData::push_fifo_data(Layers source, Layers destination, FIFORole role, FIFOData const& data, unsigned int n) {
    for (unsigned int i = 0; i < n; ++i) {
        _fifo_data[source][destination][role].push_back(data);
    }
}

void DedupData::dump() {
    std::cout << "DedupData at " << this << std::endl <<
                "\t_input_filename = " << _input_filename << std::endl << 
                "\t_output_filename = " << _output_filename << std::endl << 
                "\t_nb_threads = " << _nb_threads << std::endl <<
                "\t_compression = " << (int)_compression << std::endl <<
                "\t_preloading = " << _preloading << std::endl <<
                "\t_algorithm = " << (int)_algorithm << std::endl << 
                "\t_debug_timestamps = " << _debug_timestamps << std::endl <<
                "\t_fifos = " << std::endl;

    for (auto& p1: _fifo_data) {
        for (auto& p2: p1.second) {
            for (auto& p3: p2.second) {
                for (auto& value: p3.second) {
                    std::cout << "\t\t" << (int)p1.first << " => " << (int)p2.first << " [" << (int)p3.first << "]: " << std::endl;
                    value.dump();
                }
            }
        }
    }
}

void DedupData::validate() {
    if (_nb_threads < 1) {
        std::cerr << "[WARN] Number of threads lower than 1, setting to 1." << std::endl;
    }

    auto throw_fn = [](std::string const& context, std::string const& role, unsigned int expected, unsigned int received) -> void {
        std::ostringstream stream;
        stream << "[" << role << " - " << context << "] Expected " << expected << " configurations, got " << received << std::endl;
        throw std::runtime_error(stream.str());
    };

    for (auto const& [src, map1]: _fifo_data) {
        for (auto const& [dst, map2]: map1) {
            std::map<FIFORole, unsigned int> counts;
            counts[FIFORole::PRODUCER] = 0;
            counts[FIFORole::CONSUMER] = 0;

            for (auto const& [role, vect]: map2) {
                counts[role] += vect.size();
            }

            switch (src) {
            case Layers::FRAGMENT:
                if (counts[FIFORole::PRODUCER] != 1) {
                    throw_fn("Fragment", "Producer", 1, counts[FIFORole::PRODUCER]);
                }

                if (counts[FIFORole::CONSUMER] != _nb_threads) {
                    throw_fn("Refine", "Consumer", _nb_threads, counts[FIFORole::CONSUMER]);
                }
                break;

            case Layers::REFINE:
            case Layers::DEDUPLICATE:
            case Layers::COMPRESS:
                if (counts[FIFORole::PRODUCER] != _nb_threads) {
                    std::string context;
                    switch (src) {
                    case Layers::REFINE:
                        context = "Refine";
                        break;

                    case Layers::DEDUPLICATE:
                        context = "Deduplicate";
                        break;

                    case Layers::COMPRESS:
                        context = "Compress";
                        break;
                    }

                    throw_fn(context, "Producer", _nb_threads, counts[FIFORole::PRODUCER]);
                }

                if (dst == Layers::REORDER) {
                    if (counts[FIFORole::CONSUMER] != 1) {
                        throw_fn("Reorder", "Consumer", 1, counts[FIFORole::CONSUMER]);
                    }
                } else {
                    if (counts[FIFORole::CONSUMER] != _nb_threads) {
                        std::string context;
                        switch (src) {
                        case Layers::REFINE:
                            context = "Deduplicate";
                            break;

                        case Layers::DEDUPLICATE:
                            context = "Compress";
                            break;
                        }

                        throw_fn(context, "Consumer", _nb_threads, counts[FIFORole::CONSUMER]);
                    }
                }
                break;

            default:
                break;
            }
        }

    }

    auto find_layer_pair = [&](Layers src, Layers dst) {
        if (auto src_it = _fifo_data.find(src); src_it != _fifo_data.end()) {
            if (auto dst_it = src_it->second.find(dst); dst_it != src_it->second.end()) {
                return dst_it;
            } else {
                std::ostringstream stream;
                stream << "Missing layer " << (int)dst << " as destination from layer " << (int)src << std::endl;
                throw std::runtime_error(stream.str());
            }
        } else {
            std::ostringstream stream;
            stream << "Missing source layer " << (int)src << std::endl;
            throw std::runtime_error(stream.str());
        }
    };

    auto require_prod = [&](Layers src, Layers dst) {
        auto p = find_layer_pair(src, dst);

        if (p->second.find(FIFORole::PRODUCER) == p->second.end()) {
            std::ostringstream stream;
            stream << "Missing producer role from layer " << (int)src << " to layer " << (int)dst << std::endl;
            throw std::runtime_error(stream.str());
        }
    };

    auto require_cons = [&](Layers src, Layers dst) {
        auto p = find_layer_pair(src, dst);

        if (p->second.find(FIFORole::CONSUMER) == p->second.end()) {
            std::ostringstream stream;
            stream << "Missing consumer role from layer " << (int)src << " to layer " << (int)dst << std::endl;
            throw std::runtime_error(stream.str());
        }

    };

    auto require_prod_cons = [&](Layers src, Layers dst) {
        // Not optimal, but I don't care.
        require_prod(src, dst);
        require_cons(src, dst);
    }; 

    require_prod_cons(Layers::FRAGMENT, Layers::REFINE);
    require_prod_cons(Layers::REFINE, Layers::DEDUPLICATE);
    require_prod_cons(Layers::DEDUPLICATE, Layers::COMPRESS);
    require_prod(Layers::COMPRESS, Layers::REORDER);
    require_prod_cons(Layers::DEDUPLICATE, Layers::REORDER);

    for (auto& p1: _fifo_data) {
        for (auto& p2: p1.second) {
            for (auto& p3: p2.second) {
                for (auto value: p3.second) {
                    value.validate();
                }
            }
        }
    }
}

unsigned long long DedupData::run_orig() {
    validate();
    return EncodeDefault(*this);
}

unsigned long long DedupData::run_mutex() {
    validate();
    return EncodeMutex(*this);
}

unsigned long long DedupData::run_smart() {
    validate();
    auto [duration, datas] = EncodeSmart(*this);
    process_timestamp_data(datas);
    return duration;
}

void DedupData::process_timestamp_data(std::vector<Globals::SmartFIFOTSV> const& data) {
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
}
