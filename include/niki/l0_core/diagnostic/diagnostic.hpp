#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/** @diagnostic: 跨阶段诊断数据与事件协议
 * 这个头文件定义的是“错误如何被表达、如何被传递、如何被聚合”的统一契约。
 * 在编译器/运行时系统里，失败并不是某个阶段独有的现象：词法可能失败、语法可能失败、语义可能失败，
 * IR 校验、链接、启动执行也都可能失败。若每个阶段各自定义错误对象，最终 Driver 只能面对一堆无法合并的异构数据。
 * 因此 diagnostic 模块的第一职责是建立统一的数据平面：Diagnostic / SourceSpan / DiagnosticBag。
 *
 * 从系统设计看，diagnostic 处理的是“控制面信息”而不是“执行面数据”。
 * 执行面关心 token、AST、IR、chunk；控制面关心失败发生在何处、严重级别如何、是否可继续。
 * 这就是为什么 `DiagnosticStage`、`DiagnosticSeverity`、`code`、`message`、`notes` 要集中定义在同一层：
 * 它们构成了跨阶段稳定传输的最小字段集合。
 *
 * 这个模块采用事件化输入（`events::*Event` + `Event variant`）也有明确工程价值：
 * 上游阶段只需提交“本阶段原生错误码 + 文本 + 位置信息”，无需理解统一编码细节；
 * `DiagnosticBag::emit` 负责把事件归一为通用 `Diagnostic`。
 * 这种做法把“错误产生”与“错误标准化”解耦，降低阶段间耦合度，也让新增阶段更容易接入。
 *
 * 从算法与复杂度角度，DiagnosticBag 的语义非常简单而稳定：
 * - `add/emit` 是 O(1) 追加；
 * - `merge` 是线性拼接；
 * - `hasErrors` 提供短路判断，便于上层快速终止流水线。
 * 这意味着错误处理路径对主流程性能扰动较小，同时能保留完整上下文。
 *
 * `codeOf(...)` 的存在并非语法糖，它是“强类型枚举 -> 稳定文本错误码”的映射桥。
 * 测试可以断言错误码，日志/终端可以展示统一字符串，二者都不需要依赖脆弱的完整错误文案匹配。
 * 这对长期维护非常关键：允许改文案而不破坏测试契约。
 *
 * 总结来说，diagnostic.hpp 做的是“全链路失败语义的统一协议层”。
 * 没有这层，项目会陷入每阶段各说各话；有了这层，错误可以被可靠汇总、稳定渲染、可测可演进。
 */
namespace niki::diagnostic {

enum class DiagnosticStage : uint8_t {
    Scanner,
    Parser,
    Semantic,
    IR,
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
    ExpectedExpression,
    ExpectedStatement,
    ExpectedSemicolon,
    ExpectedIdentifier,
    UnexpectedToken,
};

enum class SemanticCode : uint8_t {
    GenericError,
    TypeMismatch,
    UndeclaredIdentifier,
    DuplicateDeclaration,
    ArgumentCountMismatch,
    ReturnTypeMismatch,
    MissingTypeAnnotation,
    NotABoolContext,
    InvalidUnaryOperand,
    AssignmentToConst,
    UseOfMovedValue,
};

enum class IRCode : uint8_t {
    InvalidRoot,
    VerifyFailed,
    LowerFailed,
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

struct IREvent {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    IRCode code = IRCode::GenericError;
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

using Event = std::variant<ScannerEvent,
                           ParserEvent,
                           SemanticEvent,
                           IREvent,
                           LinkerEvent,
                           LauncherEvent,
                           DriverEvent>;

inline Event makeError(ScannerCode code, std::string message, SourceSpan span = {}) {
    return ScannerEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(ParserCode code, std::string message, SourceSpan span = {}) {
    return ParserEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(SemanticCode code, std::string message, SourceSpan span = {}) {
    return SemanticEvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
}

inline Event makeError(IRCode code, std::string message, SourceSpan span = {}) {
    return IREvent{DiagnosticSeverity::Error, code, std::move(message), std::move(span)};
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

inline Event makeWarning(IRCode code, std::string message, SourceSpan span = {}) {
    return IREvent{DiagnosticSeverity::Warning, code, std::move(message), std::move(span)};
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

inline Event makeInfo(IRCode code, std::string message, SourceSpan span = {}) {
    return IREvent{DiagnosticSeverity::Info, code, std::move(message), std::move(span)};
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
std::string_view codeOf(events::IRCode code);
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
