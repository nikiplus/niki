#include "niki/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <utility>
/*
为什么这里有很多 std::move（详细版笔记）：

------------------------------------------------------------
0) 先给一个“够用定义”
------------------------------------------------------------
- 左值（lvalue）：有稳定身份、可取地址、通常可重复使用。
  直觉上就是“有名字、还要继续用”的对象。
- 右值（rvalue）：临时值或将亡值，通常用于初始化后就不再单独使用。

最小例子：
std::string s = "hello";   // s 是左值
std::string("hello");      // 临时对象，是右值

------------------------------------------------------------
1) 拷贝 vs 移动，到底省在哪里
------------------------------------------------------------
对于 std::string / std::vector 这类“管理堆资源”的类型：
- 拷贝：通常需要分配新内存并复制内容，代价较高；
- 移动：通常只是转移内部指针/长度/容量等控制信息，代价较低。

例子：
std::string a = "abcdef";
std::string b = a;            // 拷贝构造：a 和 b 各有一份资源
std::string c = std::move(a); // 移动构造：c 接管 a 的资源（通常更省）

被移动后的 a 仍“有效”，但值处于“未指定状态”：
- 可以销毁、可以重新赋值；
- 不应依赖其原始内容。

------------------------------------------------------------
2) std::move 到底做了什么
------------------------------------------------------------
std::move 本身不移动数据，它只是一个类型转换：
- 把表达式转换为“可移动的右值（xvalue）”；
- 让后续重载决议优先匹配移动构造/移动赋值。

所以“移动是否发生”取决于目标类型是否提供并选择了移动操作。
如果类型没有移动能力，或对象是 const，最后仍可能退化为拷贝。

------------------------------------------------------------
3) 为什么本文件使用“按值接收 + 内部 std::move”
------------------------------------------------------------
看 report 的签名：
report(..., std::string code, std::string message, SourceSpan span)

它的优势是接口统一、调用简单，然后在函数内部统一 move 进成员。
一次调用的生命周期可拆成两段：

A. 调用方 -> 形参（code/message/span）
   - 若调用方传左值：这里通常发生拷贝；
   - 若调用方传右值/临时值：这里通常可直接移动构造。

B. 形参 -> Diagnostic 成员
   - 形参在函数体里都是“有名字的变量”，因此它们本身是左值；
   - 若不写 std::move，会走拷贝；
   - 写 std::move(code/message/span) 后，显式转为可移动右值，优先走移动。

因此这套写法的目标是：
- 保持 API 简洁；
- 对右值调用保持高效；
- 对左值调用也只在必要位置产生拷贝。

------------------------------------------------------------
4) 对应到当前调用链
------------------------------------------------------------
addError/addWarning/addInfo
    -> reportError/reportWarning/reportInfo
    -> report
    -> diagnostics_.push_back(...)

每一层都使用 std::move 继续“向下传递可移动性”，
尽量避免在中间层把临时对象又变回昂贵拷贝。

------------------------------------------------------------
5) 新手常见误区（避免踩坑）
------------------------------------------------------------
1) “右值 = 字面量”不完全准确。
   右值包含临时对象和将亡值，不只字面量。
2) “用了 std::move 就一定更快”不绝对。
   对小对象/不可移动对象，收益可能不明显。
3) “被 move 后对象就废了”不准确。
   它仍是有效对象，只是值未指定，别再依赖旧内容。
4) “const 对象也能高效 move”通常不成立。
   const 对象常无法绑定到可修改的移动构造，可能退化为拷贝。
*/
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
