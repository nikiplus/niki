#include "niki/l0_core/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace niki::diagnostic {

namespace {

constexpr std::string_view kScannerInvalidToken = "SCANNER_INVALID_TOKEN";
constexpr std::string_view kParserGenericError = "PARSER_ERROR";
constexpr std::string_view kSemanticGenericError = "SEMANTIC_ERROR";
constexpr std::string_view kCompilerInvalidRoot = "COMPILER_INVALID_ROOT";
constexpr std::string_view kCompilerGenericError = "COMPILER_ERROR";
constexpr std::string_view kLinkerDuplicateSymbol = "LINKER_DUPLICATE_SYMBOL";
constexpr std::string_view kLinkerMultipleEntry = "LINKER_MULTIPLE_ENTRY";
constexpr std::string_view kLinkerEntryNotFound = "LINKER_ENTRY_NOT_FOUND";
constexpr std::string_view kLauncherInitRuntimeError = "LAUNCHER_INIT_RUNTIME_ERROR";
constexpr std::string_view kLauncherEntryLookupFailed = "LAUNCHER_ENTRY_LOOKUP_FAILED";
constexpr std::string_view kLauncherEntryRuntimeError = "LAUNCHER_ENTRY_RUNTIME_ERROR";
constexpr std::string_view kDriverIoError = "DRIVER_IO_ERROR";
constexpr std::string_view kDriverNoInput = "DRIVER_NO_INPUT";

SourceSpan normalizeSpan(SourceSpan span) {
    if ((span.line > 0 || span.column > 0) && span.file.empty()) {
        span.file = "<unknown>";
    }
    return span;
}

} // namespace

std::string_view codeOf(events::ScannerCode code) {
    switch (code) {
    case events::ScannerCode::InvalidToken:
        return kScannerInvalidToken;
    }
    return kScannerInvalidToken;
}

std::string_view codeOf(events::ParserCode code) {
    switch (code) {
    case events::ParserCode::GenericError:
        return kParserGenericError;
    }
    return kParserGenericError;
}

std::string_view codeOf(events::SemanticCode code) {
    switch (code) {
    case events::SemanticCode::GenericError:
        return kSemanticGenericError;
    }
    return kSemanticGenericError;
}

std::string_view codeOf(events::CompilerCode code) {
    switch (code) {
    case events::CompilerCode::InvalidRoot:
        return kCompilerInvalidRoot;
    case events::CompilerCode::GenericError:
        return kCompilerGenericError;
    }
    return kCompilerGenericError;
}

std::string_view codeOf(events::LinkerCode code) {
    switch (code) {
    case events::LinkerCode::DuplicateSymbol:
        return kLinkerDuplicateSymbol;
    case events::LinkerCode::MultipleEntry:
        return kLinkerMultipleEntry;
    case events::LinkerCode::EntryNotFound:
        return kLinkerEntryNotFound;
    }
    return kLinkerEntryNotFound;
}

std::string_view codeOf(events::LauncherCode code) {
    switch (code) {
    case events::LauncherCode::InitRuntimeError:
        return kLauncherInitRuntimeError;
    case events::LauncherCode::EntryLookupFailed:
        return kLauncherEntryLookupFailed;
    case events::LauncherCode::EntryRuntimeError:
        return kLauncherEntryRuntimeError;
    }
    return kLauncherEntryRuntimeError;
}

std::string_view codeOf(events::DriverCode code) {
    switch (code) {
    case events::DriverCode::IoError:
        return kDriverIoError;
    case events::DriverCode::NoInput:
        return kDriverNoInput;
    }
    return kDriverIoError;
}

SourceSpan makeSourceSpan(std::string file, uint32_t line, uint32_t column, uint32_t length) {
    return SourceSpan{std::move(file), line, column, length};
}

void DiagnosticBag::add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

void DiagnosticBag::emit(events::Event event) {
    std::visit(
        [this](auto &&typed_event) {
            using T = std::decay_t<decltype(typed_event)>;
            if constexpr (std::is_same_v<T, events::ScannerEvent>) {
                diagnostics_.push_back({DiagnosticStage::Scanner, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::ParserEvent>) {
                diagnostics_.push_back({DiagnosticStage::Parser, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::SemanticEvent>) {
                diagnostics_.push_back({DiagnosticStage::Semantic, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::CompilerEvent>) {
                diagnostics_.push_back({DiagnosticStage::Compiler, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::LinkerEvent>) {
                diagnostics_.push_back({DiagnosticStage::Linker, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::LauncherEvent>) {
                diagnostics_.push_back({DiagnosticStage::Launcher, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            } else if constexpr (std::is_same_v<T, events::DriverEvent>) {
                diagnostics_.push_back({DiagnosticStage::Driver, typed_event.severity, std::string(codeOf(typed_event.code)),
                                        std::move(typed_event.message), normalizeSpan(std::move(typed_event.span)), {}});
            }
        },
        std::move(event));
}

void DiagnosticBag::merge(const DiagnosticBag &other) {
    diagnostics_.insert(diagnostics_.end(), other.diagnostics_.begin(), other.diagnostics_.end());
}

void DiagnosticBag::merge(DiagnosticBag &&other) {
    diagnostics_.insert(diagnostics_.end(), std::make_move_iterator(other.diagnostics_.begin()),
                        std::make_move_iterator(other.diagnostics_.end()));
    other.diagnostics_.clear();
}

bool DiagnosticBag::hasErrors() const {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(),
                       [](const Diagnostic &diagnostic) { return diagnostic.severity == DiagnosticSeverity::Error; });
}

bool DiagnosticBag::empty() const { return diagnostics_.empty(); }

size_t DiagnosticBag::size() const { return diagnostics_.size(); }

const std::vector<Diagnostic> &DiagnosticBag::all() const { return diagnostics_; }

std::vector<Diagnostic> DiagnosticBag::takeAll() {
    std::vector<Diagnostic> out = std::move(diagnostics_);
    diagnostics_.clear();
    return out;
}

std::string_view toString(DiagnosticStage stage) {
    switch (stage) {
    case DiagnosticStage::Scanner:
        return "scanner";
    case DiagnosticStage::Parser:
        return "parser";
    case DiagnosticStage::Semantic:
        return "semantic";
    case DiagnosticStage::Compiler:
        return "compiler";
    case DiagnosticStage::Linker:
        return "linker";
    case DiagnosticStage::Launcher:
        return "launcher";
    case DiagnosticStage::Driver:
        return "driver";
    default:
        return "unknown";
    }
}

std::string_view toString(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Info:
        return "info";
    default:
        return "error";
    }
}

} // namespace niki::diagnostic
