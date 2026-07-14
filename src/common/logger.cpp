#include "logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace omnigpu {

void init_logger() {
    auto console = spdlog::stdout_color_mt("omnigpu");
    console->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
    console->set_level(spdlog::level::trace);
    spdlog::set_default_logger(console);
}

} // namespace omnigpu
