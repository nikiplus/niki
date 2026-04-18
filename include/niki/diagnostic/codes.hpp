#pragma once

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
