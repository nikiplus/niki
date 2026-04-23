#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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

class DiagnosticBag {
  public:
    void add(Diagnostic diagnostic);
    void report(DiagnosticStage stage, DiagnosticSeverity severity, std::string code, std::string message,
                SourceSpan span = {});
    void reportError(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});
    void reportWarning(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});
    void reportInfo(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});

    void addError(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});
    void addWarning(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});
    void addInfo(DiagnosticStage stage, std::string code, std::string message, SourceSpan span = {});

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
