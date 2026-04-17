#include "niki/debug/diagnostic_logger.hpp"

#include "niki/debug/logger.hpp"
#include "niki/diagnostic/renderer.hpp"

namespace niki::debug {

void logDiagnostic(const niki::diagnostic::Diagnostic &diagnostic) {
    const std::string text = niki::diagnostic::renderDiagnosticText(diagnostic);
    switch (diagnostic.severity) {
    case niki::diagnostic::DiagnosticSeverity::Error:
        error("diagnostic", "{}", text);
        break;
    case niki::diagnostic::DiagnosticSeverity::Warning:
        warn("diagnostic", "{}", text);
        break;
    case niki::diagnostic::DiagnosticSeverity::Info:
        info("diagnostic", "{}", text);
        break;
    }
}

void logDiagnosticBag(const niki::diagnostic::DiagnosticBag &bag) {
    for (const auto &diagnostic : bag.all()) {
        logDiagnostic(diagnostic);
    }
}

} // namespace niki::debug
