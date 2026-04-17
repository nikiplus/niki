#pragma once
#include <fmt/base.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace niki::debug {
void initLogger(spdlog::level::level_enum level, std::string_view file_path);
void shutdownLogger();

template <typename... Args> inline void trace(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    if (!spdlog::should_log(spdlog::level::trace)) {
        return;
    }
    spdlog::trace("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

template <typename... Args> inline void debug(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    if (!spdlog::should_log(spdlog::level::debug)) {
        return;
    }
    spdlog::debug("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

template <typename... Args> inline void info(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::info("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

template <typename... Args> inline void warn(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::warn("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

template <typename... Args> inline void error(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::error("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}
} // namespace niki::debug
