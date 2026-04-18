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

static std::string escapeJsonString(std::string_view input) {
    std::ostringstream oss;
    for (const char c : input) {
        switch (c) {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << c;
            break;
        }
    }
    return oss.str();
}

std::string renderDiagnosticJson(const Diagnostic &diagnostic) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"severity\":\"" << toString(diagnostic.severity) << "\",";
    oss << "\"stage\":\"" << toString(diagnostic.stage) << "\",";
    oss << "\"code\":\"" << escapeJsonString(diagnostic.code) << "\",";
    oss << "\"message\":\"" << escapeJsonString(diagnostic.message) << "\",";
    oss << "\"span\":{"
        << "\"file\":\"" << escapeJsonString(diagnostic.span.file) << "\","
        << "\"line\":" << diagnostic.span.line << ","
        << "\"column\":" << diagnostic.span.column << ","
        << "\"length\":" << diagnostic.span.length << "},";
    oss << "\"notes\":[";
    for (size_t i = 0; i < diagnostic.notes.size(); ++i) {
        oss << "\"" << escapeJsonString(diagnostic.notes[i]) << "\"";
        if (i + 1 < diagnostic.notes.size()) {
            oss << ",";
        }
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::string renderDiagnosticBagJson(const DiagnosticBag &bag) {
    std::ostringstream oss;
    oss << "[";
    const auto &all = bag.all();
    for (size_t i = 0; i < all.size(); ++i) {
        oss << renderDiagnosticJson(all[i]);
        if (i + 1 < all.size()) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

} // namespace niki::diagnostic
