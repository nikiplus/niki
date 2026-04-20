#pragma once
#include "niki/syntax/ast.hpp"
#include "niki/syntax/ast_payloads.hpp"
#include "niki/syntax/global_interner.hpp"
#include "niki/syntax/token.hpp"
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