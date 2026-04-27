#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace niki::diagnostic {

enum class DiagnosticStage : uint8_t {
    Scanner,
    Parser,
    Semantic,
    Compiler,
    Linker,
    Launcher,
    Driver,
    Unknown,
};

enum class DiagnosticSeverity : uint8_t {
    Error,
    Warning,
    Info,
};

struct SourceSpan {
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t length = 0;
};

SourceSpan makeSourceSpan(std::string file = "", uint32_t line = 0, uint32_t column = 0, uint32_t length = 0);

struct Diagnostic {
    DiagnosticStage stage = DiagnosticStage::Unknown;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    SourceSpan span;
    std::vector<std::string> notes;
};

namespace events {

enum class ScannerCode : uint8_t {
    InvalidToken,
};

enum class ParserCode : uint8_t {
    GenericError,
};

enum class SemanticCode : uint8_t {
    GenericError,
};

enum class CompilerCode : uint8_t {
    InvalidRoot,
    GenericError,
};

enum class LinkerCode : uint8_t {
    DuplicateSymbol,
    MultipleEntry,
    EntryNotFound,
};

enum class LauncherCode : uint8_t {
    InitRuntimeError,
    EntryLookupFailed,
    EntryRuntimeError,
};

enum class DriverCode : uint8_t {
    IoError,
    NoInput,
};

struct ScannerEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    ScannerCode code = ScannerCode::InvalidToken;
    std::string message;
    SourceSpan span;
};

struct ParserEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    ParserCode code = ParserCode::GenericError;
    std::string message;
    SourceSpan span;
};

struct SemanticEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SemanticCode code = SemanticCode::GenericError;
    std::string message;
    SourceSpan span;
};

struct CompilerEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    CompilerCode code = CompilerCode::GenericError;
    std::string message;
    SourceSpan span;
};

struct LinkerEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    LinkerCode code = LinkerCode::EntryNotFound;
    std::string message;
    SourceSpan span;
};

struct LauncherEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    LauncherCode code = LauncherCode::EntryRuntimeError;
    std::string message;
    SourceSpan span;
};

struct DriverEvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    DriverCode code = DriverCode::IoError;
    std::string message;
    SourceSpan span;
};

using Event =
    std::variant<ScannerEvent, ParserEvent, SemanticEvent, CompilerEvent, LinkerEvent, LauncherEvent, DriverEvent>;

inline Event makeError(ScannerCode code, std::string message, SourceSpan span = {}) {
    return ScannerEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(ParserCode code, std::string message, SourceSpan span = {}) {
    return ParserEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(SemanticCode code, std::string message, SourceSpan span = {}) {
    return SemanticEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(CompilerCode code, std::string message, SourceSpan span = {}) {
    return CompilerEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(LinkerCode code, std::string message, SourceSpan span = {}) {
    return LinkerEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(LauncherCode code, std::string message, SourceSpan span = {}) {
    return LauncherEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(DriverCode code, std::string message, SourceSpan span = {}) {
    return DriverEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeWarning(ScannerCode code, std::string message, SourceSpan span = {}) {
    return ScannerEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(ParserCode code, std::string message, SourceSpan span = {}) {
    return ParserEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(SemanticCode code, std::string message, SourceSpan span = {}) {
    return SemanticEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(CompilerCode code, std::string message, SourceSpan span = {}) {
    return CompilerEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(LinkerCode code, std::string message, SourceSpan span = {}) {
    return LinkerEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(LauncherCode code, std::string message, SourceSpan span = {}) {
    return LauncherEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeWarning(DriverCode code, std::string message, SourceSpan span = {}) {
    return DriverEvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
}

inline Event makeInfo(ScannerCode code, std::string message, SourceSpan span = {}) {
    return ScannerEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(ParserCode code, std::string message, SourceSpan span = {}) {
    return ParserEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(SemanticCode code, std::string message, SourceSpan span = {}) {
    return SemanticEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(CompilerCode code, std::string message, SourceSpan span = {}) {
    return CompilerEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(LinkerCode code, std::string message, SourceSpan span = {}) {
    return LinkerEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(LauncherCode code, std::string message, SourceSpan span = {}) {
    return LauncherEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

inline Event makeInfo(DriverCode code, std::string message, SourceSpan span = {}) {
    return DriverEvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
}

} // namespace events

std::string_view codeOf(events::ScannerCode code);
std::string_view codeOf(events::ParserCode code);
std::string_view codeOf(events::SemanticCode code);
std::string_view codeOf(events::CompilerCode code);
std::string_view codeOf(events::LinkerCode code);
std::string_view codeOf(events::LauncherCode code);
std::string_view codeOf(events::DriverCode code);

class DiagnosticBag {
  public:
    void add(Diagnostic diagnostic);
    void emit(events::Event event);
    template <typename Code> void error(Code code, std::string message, SourceSpan span = {}) {
        emit(events::makeError(code, std::move(message), std::move(span)));
    }
    template <typename Code> void warning(Code code, std::string message, SourceSpan span = {}) {
        emit(events::makeWarning(code, std::move(message), std::move(span)));
    }
    template <typename Code> void info(Code code, std::string message, SourceSpan span = {}) {
        emit(events::makeInfo(code, std::move(message), std::move(span)));
    }

    void merge(const DiagnosticBag &other);
    void merge(DiagnosticBag &&other);

    bool hasErrors() const;
    bool empty() const;
    size_t size() const;

    const std::vector<Diagnostic> &all() const;
    std::vector<Diagnostic> takeAll();

  private:
    std::vector<Diagnostic> diagnostics_;
};

std::string_view toString(DiagnosticStage stage);
std::string_view toString(DiagnosticSeverity severity);

} // namespace niki::diagnostic
