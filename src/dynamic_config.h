#ifndef DYNAMIC_CONFIG_H
#define DYNAMIC_CONFIG_H

#include <fstream>
#include <iostream>
#include <variant>

#include "utils.h"

namespace ExtraConfig {
    std::ostream& runs_times_file();
    std::ostream& iterations_times_file();
}

class DynamicConfig {
public:
    struct SynchronizationPatterns {
        bool _sequential = false;
        bool _alt_bit = false;
        bool _counter = false;
        bool _block = false;
        bool _block_plus = false;
        bool _jline = false;
        bool _jline_plus = false;
        bool _increasing_jline = false;
        bool _increasing_jline_plus = false;
        bool _kline = false;
        bool _kline_plus = false;
        bool _increasing_kline = false;
        bool _increasing_kline_plus = false;
    };

    class Files {
    public:
        std::ostream& runs_times_file() {
            return *_runs_times_file;
        }

        std::ostream& iterations_times_file() {
            return *_iterations_times_file;
        }

        void set_runs_times_file(std::ostream& o) {
            _runs_times_file.reset(o);
        }

        void set_iterations_times_file(std::ostream& o) {
            _iterations_times_file.reset(o);
        }

        void set_simulations_filename(std::string const& filename) {
            _simulations_filename = filename;
        }

        std::string const& get_simulations_filename() const {
            return _simulations_filename;
        }

    private:
        class EitherCoutOr {
        public:
            EitherCoutOr() {
                
            }

            NO_COPY(EitherCoutOr);

            ~EitherCoutOr() {
                if (_stream)
                    dynamic_cast<std::ofstream*>(_stream.get())->close();
            }

            std::ostream& operator*() {
                if (_stream) {
                    return *_stream.get();
                } else {
                    return std::cout;
                }
            }

            void reset(std::ostream& o) {
                if (&o != &std::cout) {
                    _stream.reset(&o);
                }
            }

        private:
            std::unique_ptr<std::ostream> _stream;
        };

        EitherCoutOr _runs_times_file;
        EitherCoutOr _iterations_times_file;
        std::string _simulations_filename;
    };

    struct Extra {
        unsigned int _increasing_jline_step;
        unsigned int _static_step_jline_plus;
    };

public:
    NO_COPY(DynamicConfig);

    friend void parse_command_line(int, char**);
    friend void parse_environ();
    friend std::ostream& ExtraConfig::runs_times_file();
    friend std::ostream& ExtraConfig::iterations_times_file();

    static inline const DynamicConfig& instance() {
        return _instance();
    }

    SynchronizationPatterns _patterns;
    Files _files;
    Extra _extra;

private:
    DynamicConfig() { }

    static DynamicConfig& _instance() {
        static DynamicConfig instance;
        return instance;
    }
};



#define sDynamicConfig DynamicConfig::instance()
#define sDynamicConfigPatterns sDynamicConfig._patterns
#define sDynamicConfigFiles sDynamicConfig._files
#define sDynamicConfigExtra sDynamicConfig._extra

#endif // DYNAMIC_CONFIG_H
