#pragma once
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <string>
#include <vector>

namespace niki {
struct GlobalCompilationUnit {
    std::string source_path;
    std::string source;
    std::vector<syntax::Token> tokens;

    // 每个单元仍保留自己的astpool
    syntax::ASTPool pool;
    syntax::ASTNodeIndex root = syntax::ASTNodeIndex::invalid();

    explicit GlobalCompilationUnit(syntax::GlobalInterner &interner) : pool(interner) {}
};
} // namespace niki