#include "niki/l0_core/diagnostic/renderer.hpp"

#include <sstream>
#include <string>

namespace niki::diagnostic {
namespace {

std::string escapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
        case '\"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

} // namespace

std::string renderDiagnosticText(const Diagnostic &diagnostic) {
    std::ostringstream os;
    os << "[" << toString(diagnostic.severity) << "][" << toString(diagnostic.stage) << "][" << diagnostic.code << "] ";
    if (!diagnostic.span.file.empty()) {
        os << diagnostic.span.file << ":" << diagnostic.span.line << ":" << diagnostic.span.column << " ";
    }
    os << diagnostic.message;
    for (const auto &note : diagnostic.notes) {
        os << "\n  note: " << note;
    }
    return os.str();
}

std::string renderDiagnosticBagText(const DiagnosticBag &bag) {
    std::ostringstream os;
    const auto &all = bag.all();
    for (size_t i = 0; i < all.size(); ++i) {
        os << renderDiagnosticText(all[i]);
        if (i + 1 < all.size()) {
            os << "\n";
        }
    }
    return os.str();
}

std::string renderDiagnosticJson(const Diagnostic &diagnostic) {
    std::ostringstream os;
    os << "{"
       << "\"severity\":\"" << escapeJson(toString(diagnostic.severity)) << "\","
       << "\"stage\":\"" << escapeJson(toString(diagnostic.stage)) << "\","
       << "\"code\":\"" << escapeJson(diagnostic.code) << "\","
       << "\"message\":\"" << escapeJson(diagnostic.message) << "\","
       << "\"span\":{"
       << "\"file\":\"" << escapeJson(diagnostic.span.file) << "\","
       << "\"line\":" << diagnostic.span.line << ","
       << "\"column\":" << diagnostic.span.column << ","
       << "\"length\":" << diagnostic.span.length << "},"
       << "\"notes\":[";
    for (size_t i = 0; i < diagnostic.notes.size(); ++i) {
        os << "\"" << escapeJson(diagnostic.notes[i]) << "\"";
        if (i + 1 < diagnostic.notes.size()) {
            os << ",";
        }
    }
    os << "]}";
    return os.str();
}

std::string renderDiagnosticBagJson(const DiagnosticBag &bag) {
    std::ostringstream os;
    os << "[";
    const auto &all = bag.all();
    for (size_t i = 0; i < all.size(); ++i) {
        os << renderDiagnosticJson(all[i]);
        if (i + 1 < all.size()) {
            os << ",";
        }
    }
    os << "]";
    return os.str();
}

} // namespace niki::diagnostic
