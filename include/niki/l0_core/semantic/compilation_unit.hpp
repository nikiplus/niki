#pragma once
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/string_interner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <string>
#include <vector>

namespace niki {
struct CompilationUnit {
    std::string source_path;           ///< 源文件路径。
    std::string source;                ///< 源文件文本内容。
    std::vector<syntax::Token> tokens; ///< 词法输出 token 序列。

    syntax::ASTPool pool;                                        ///< 单元 AST 池（绑定共享 interner）。
    syntax::ASTNodeIndex root = syntax::ASTNodeIndex::invalid(); ///< AST 根节点索引。

    ModuleId module_id = kInvalidModuleId; ///< 模块稳定身份标识（由 ModuleIdAllocator 在 FileScan 阶段分配）。

    /** @brief 构造编译单元并注入共享 interner。 */
    explicit CompilationUnit(syntax::StringInterner &interner) : pool(interner) {}
};
} // namespace niki