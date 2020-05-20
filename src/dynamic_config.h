#ifndef DYNAMIC_CONFIG_H
#define DYNAMIC_CONFIG_H

#include <fstream>
#include <iostream>
#include <variant>

#include "utils.h"

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
                return *_stream.get();
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
    };

public:
    NO_COPY(DynamicConfig);

    friend void parse_command_line(int, char**);
    friend void parse_environ();

    static inline const DynamicConfig& instance() {
        return _instance();
    }

    SynchronizationPatterns _patterns;
    Files _files;

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

#endif // DYNAMIC_CONFIG_H