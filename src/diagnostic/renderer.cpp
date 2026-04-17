#include "niki/diagnostic/renderer.hpp"

#include <sstream>

namespace niki::diagnostic {

std::string renderDiagnosticText(const Diagnostic &diagnostic) {
    std::ostringstream oss;
    oss << "[" << toString(diagnostic.severity) << "][" << toString(diagnostic.stage) << "]";
    if (!diagnostic.code.empty()) {
        oss << "[" << diagnostic.code << "]";
    }
    if (!diagnostic.span.file.empty()) {
        oss << " " << diagnostic.span.file;
    }
    if (diagnostic.span.line > 0) {
        oss << ":" << diagnostic.span.line << ":" << diagnostic.span.column;
    }
    oss << " " << diagnostic.message;
    for (const auto &note : diagnostic.notes) {
        oss << "\n  note: " << note;
    }
    return oss.str();
}

std::string renderDiagnosticBagText(const DiagnosticBag &bag) {
    std::ostringstream oss;
    const auto &all = bag.all();
    for (size_t i = 0; i < all.size(); ++i) {
        oss << renderDiagnosticText(all[i]);
        if (i + 1 < all.size()) {
            oss << "\n";
        }
    }
    return oss.str();
}

} // namespace niki::diagnostic
