#include "niki/debug/logger.hpp"
#include <cstdlib>
#include <iterator>
#include <memory>
#include <spdlog/async.h>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

namespace niki::debug {
static spdlog::level::level_enum parseLevel(std::string_view s) {
    if (s == "trace")
        return spdlog::level::trace;
    if (s == "debug")
        return spdlog::level::debug;
    if (s == "warn")
        return spdlog::level::warn;
    if (s == "error")
        return spdlog::level::err;
    return spdlog::level::info;
}
void initLogger(spdlog::level::level_enum level, std::string_view file_path) {
    if (const char *env = std::getenv("NIKI_LOG_LEVEL")) {
        level = parseLevel(env);
    }
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(std::string(file_path), 5 * 1024 * 1024, 3);
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

    auto logger = std::make_shared<spdlog::logger>("niki", sinks.begin(), sinks.end());
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d%H:%M:%S.%e][%^%l%$][%t][%n] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(level);
    spdlog::flush_on(spdlog::level::warn);
}
void shutdownLogger() { spdlog::shutdown(); }
} // namespace niki::debug