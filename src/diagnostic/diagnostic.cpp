#include "niki/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <utility>
/*我们将值的接取方式分为拷贝和非拷贝两种吧——不然难以理解为什么这里有这么多std::move
假设我们要求函数获取一个值，其获取方式只有两种——将值取过来(也即拷贝一次)，以及只读值(也即获得该值的索引)
那么在当前场景下，我们的报错信息事实上是‘一次性’的，也即，我们并不需要维护值的持久性——它是用完即丢的。
因此我们要将值直接抓取过来，将其所有权从来源转移到我们的struct diagnositc诊断包中去
在这里会发生一个新的问题。
我们的一个值被struct所有时，其底层事实上发生了第二次拷贝，我们可以画一个图来进行简单的理解。

string::any
    ↓copy to            left     assign       right
  report            data:report <——————data:string any
    ↓copy to            left     assign       right
struct diagnositc    data:struct <——————data:report
也就是说，我们的底层编译器在让函数的结构体获取到这个值的时候，并不是直接就让其被赋值到了struct之上，而是要先抓取再赋值(因为此时被抓取来的值是一个左值)
*什么是左值？什么是右值？
观察这段表达式
b=1;
a= b;
return a;//a=1
在这里我们可以借用rust的思路来进行简单理解,在这里，b值首先作为左值，获得了数据“1”的所有权。
此时b为被赋予者，我们可以将其视为值的所有者。
在行段2中，b变为了右值，而左值变为a，此时b作为值的赋予者，a作为值的被赋予者，那么此事a就成了数据1的所有者。
注意到吗？这里的数据所有者被转移了。
最终，我们仅返回一个a的值，而b的值由于我们不需要，在这次函数结束的时候其就被释放掉——被抛弃了！
那么这里我们通过两次赋值使a获得了数据“1”的所有权，但这两次赋值的代价是高昂的——它相当于对值进行了两次拷贝，并分配了一个空位来专门给临时的b使用。
那么事实上，我们就可以消灭掉这个b
a = 1;
return a；a=1
结果是完全一样的！
那么在实际工程中，我们不可避免的要面对一些传进来的数据，而这些数据想要被使用，就不得不经历我们上面所说的那一长串copy
因此工程师们就思考，我能否发明一种语法，让一个值可以直接被视作右值？以让其它结构体在访问该值时明确，可直接接管该值的所有权，而无需拷贝？
那么std::move 语法就诞生了。
它将被传入函数的值直接视作右值，那么结构体在实际调用该值时，就无需在内存上创建变量进行一次拷贝，而是直接将其拿过来，直接塞进自己的内存池中。
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
