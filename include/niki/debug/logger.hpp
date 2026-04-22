#pragma once
#include <fmt/base.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace niki::debug {
// ---- 为什么“设置一个 level”就能打印不同细度？----
// spdlog 内部有日志等级阈值（trace < debug < info < warn < error）。
// 当全局 level 设为 info 时：
// - trace/debug 会被过滤，不输出
// - info/warn/error 会输出
// 这就是“同一套接口，按等级自动筛选输出细度”的原理。
//
// ---- 这里的模板写法在做什么？----
// 形如：
//   template <typename... Args>
//   fmt::format_string<Args...> f, Args&&... args
// 含义是“格式串 + 可变参数包”。
// 关键点：
// 1) fmt::format_string<Args...> 会在编译期检查占位符与参数类型是否匹配（更安全）。
// 2) Args&&... + std::forward(...) 保留左值/右值属性，避免不必要拷贝（更高效）。
// 3) 统一包装后，业务层只需写：debug("parser", "token={} line={}", t, line)。
//
// 日志系统初始化：
// - level: 默认日志级别（可被环境变量 NIKI_LOG_LEVEL 覆盖）
// - file_path: 日志文件路径（内部会创建滚动文件 sink）
// 典型在 main() 启动阶段调用一次。
void initLogger(spdlog::level::level_enum level, std::string_view file_path);

// 日志系统收尾：
// 典型在进程退出前调用一次，确保 sink 正确刷新和释放。
void shutdownLogger();

// trace: 最细粒度调试日志（通常用于高频流程、逐步跟踪）。
// 这里先 should_log 再 format，避免在低日志级别下做无意义的字符串格式化。
template <typename... Args> inline void trace(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    if (!spdlog::should_log(spdlog::level::trace)) {
        return;
    }
    spdlog::trace("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

// debug: 常规调试日志（比 trace 粗一层，适合关键状态和阶段信息）。
template <typename... Args> inline void debug(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    if (!spdlog::should_log(spdlog::level::debug)) {
        return;
    }
    spdlog::debug("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

// info: 正常运行信息（比如阶段开始/结束、统计摘要）。
template <typename... Args> inline void info(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::info("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

// warn: 可恢复问题、潜在风险。
template <typename... Args> inline void warn(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::warn("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}

// error: 错误级别日志（通常配合 Diagnostic 输出或异常路径）。
template <typename... Args> inline void error(std::string_view module, fmt::format_string<Args...> f, Args &&...args) {
    spdlog::error("[{}] {}", module, fmt::format(f, std::forward<Args>(args)...));
}
} // namespace niki::debug
