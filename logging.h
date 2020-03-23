#ifndef LOGGING_H
#define LOGGING_H

namespace Loggers {
    namespace Names {
        static constexpr const char* global_logger = "global";
    }

    namespace Files {
        static constexpr const char* global_file = "logs/global.log";
    }
}

void init_logging();

#endif /* LOGGING_H */