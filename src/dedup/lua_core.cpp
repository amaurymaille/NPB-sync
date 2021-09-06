#include <iostream>

#include "encoder.h"
#include "lua_core.h"

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

void DedupData::push_fifo_data(Layers source, Layers destination, FIFORole role, FIFOData const& data) {
    _fifo_data[source][destination][role] = data;
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
                std::cout << "\t\t" << (int)p1.first << " => " << (int)p2.first << " [" << (int)p3.first << "]: " << std::endl;
                p3.second.dump();
            }
        }
    }
}

void DedupData::validate() {
    if (_nb_threads < 1) {
        std::cerr << "[WARN] Number of threads lower than 1, setting to 1." << std::endl;
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
                p3.second.validate();
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
    return EncodeSmart(*this);
}
