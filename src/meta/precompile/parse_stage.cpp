#include "niki/meta/precompile/precompile_pipeline.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"

/** @meta_precompile_parse_impl: 预编译解析阶段实现
 * 该文件仅负责 scan+parse，把源文本转成 `GlobalCompilationUnit` 可消费结构。
 * 它不处理符号、类型和模块可见性，保持 parse 阶段单一职责。
 */
namespace niki::meta::precompile {

/**
 * @brief 对单编译单元执行扫描与解析。
 * @param unit 编译单元，输入 source/source_path，输出 tokens/root。
 * @return std::expected<void, diagnostic::DiagnosticBag> 成功返回空，失败返回词法或语法诊断。
 */
std::expected<void, diagnostic::DiagnosticBag> parseIntoCompilationUnit(GlobalCompilationUnit &unit) {
    unit.tokens.clear();
    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) {
            break;
        }
    }
    auto scanner_diagnostics = scanner.takeDiagnostics();
    if (!scanner_diagnostics.empty()) {
        return std::unexpected(std::move(scanner_diagnostics));
    }
    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    if (!parse_result.diagnostics.empty()) {
        return std::unexpected(std::move(parse_result.diagnostics));
    }
    unit.root = parse_result.root;
    return {};
}

} // namespace niki::meta::precompile
