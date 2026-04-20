#pragma once

/*这是一张“诊断码字典”
主要就是把各个阶段（scanner/parser/semantic/compiler/linker/launcher/driver）的 code 统一放在这里管理。
业务代码报错时直接引用这里的常量，测试里也直接断言这些 code，不再到处手写字符串。
这样设计的目的就三点：统一、稳定、可机器处理。
- 统一：同一类错误在全链路都用同一个 code；
- 稳定：message 怎么改都不影响 code，断言和外部对接更稳；
- 可机器处理：后续做过滤、统计、聚合都更方便。
约定：
- 新增错误码按 stage 分组追加；
- 命名使用 UPPER_SNAKE_CASE；
- 禁止在业务代码中直接拼接或硬编码错误码字符串。*/

namespace niki::diagnostic::codes {

namespace scanner {
inline constexpr const char *InvalidToken = "SCANNER_INVALID_TOKEN";
} // namespace scanner

namespace parser {
inline constexpr const char *GenericError = "PARSER_ERROR";
} // namespace parser

namespace semantic {
inline constexpr const char *GenericError = "SEMANTIC_ERROR";
} // namespace semantic

namespace compiler {
inline constexpr const char *InvalidRoot = "COMPILER_INVALID_ROOT";
inline constexpr const char *GenericError = "COMPILER_ERROR";
} // namespace compiler

namespace linker {
inline constexpr const char *DuplicateSymbol = "LINKER_DUPLICATE_SYMBOL";
inline constexpr const char *MultipleEntry = "LINKER_MULTIPLE_ENTRY";
inline constexpr const char *EntryNotFound = "LINKER_ENTRY_NOT_FOUND";
} // namespace linker

namespace launcher {
inline constexpr const char *InitRuntimeError = "LAUNCHER_INIT_RUNTIME_ERROR";
inline constexpr const char *EntryLookupFailed = "LAUNCHER_ENTRY_LOOKUP_FAILED";
inline constexpr const char *EntryRuntimeError = "LAUNCHER_ENTRY_RUNTIME_ERROR";
} // namespace launcher

namespace driver {
inline constexpr const char *IoError = "DRIVER_IO_ERROR";
inline constexpr const char *NoInput = "DRIVER_NO_INPUT";
} // namespace driver

} // namespace niki::diagnostic::codes
