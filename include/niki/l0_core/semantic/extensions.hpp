#pragma once
#include "niki/l0_core/syntax/ast.hpp"

namespace niki::semantic {
class TypeChecker;

using DomainSemanticDeclHandler = bool (*)(TypeChecker &checker, syntax::ASTNodeIndex decl_idx);

void registerDomainSemanticDeclHandler(DomainSemanticDeclHandler handler);
DomainSemanticDeclHandler getDomainSemanticDeclHandler();
} // namespace niki::semantic

