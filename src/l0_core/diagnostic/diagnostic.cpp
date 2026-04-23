#include "niki/l0_core/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <utility>

namespace niki::diagnostic {

SourceSpan makeSourceSpan(std::string file, uint32_t line, uint32_t column, uint32_t length) {
    return SourceSpan{std::move(file), line, column, length};
}

void DiagnosticBag::add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

void DiagnosticBag::report(DiagnosticStage stage, DiagnosticSeverity severity, std::string code, std::string message,
                           SourceSpan span) {
    if ((span.line > 0 || span.column > 0) && span.file.empty()) {
        span.file = "<unknown>";
    }
    diagnostics_.push_back({stage, severity, std::move(code), std::move(message), std::move(span), {}});
}

void DiagnosticBag::reportError(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    report(stage, DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(span));
}

void DiagnosticBag::reportWarning(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    report(stage, DiagnosticSeverity::Warning, std::move(code), std::move(message), std::move(span));
}

void DiagnosticBag::reportInfo(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    report(stage, DiagnosticSeverity::Info, std::move(code), std::move(message), std::move(span));
}

void DiagnosticBag::addError(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    reportError(stage, std::move(code), std::move(message), std::move(span));
}

void DiagnosticBag::addWarning(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    reportWarning(stage, std::move(code), std::move(message), std::move(span));
}

void DiagnosticBag::addInfo(DiagnosticStage stage, std::string code, std::string message, SourceSpan span) {
    reportInfo(stage, std::move(code), std::move(message), std::move(span));
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
