#include "spdlog/spdlog.h"
#include "spdlog/logger.h"
#include "spdlog/formatter.h"
#include "spdlog/pattern_formatter.h"
#include "spdlog/sinks/sink.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include "logging.h"

void init_logging()
{
    namespace names = Loggers::Names;
    namespace files = Loggers::Files;

    auto logger = spdlog::stdout_color_mt(names::global_logger);
}