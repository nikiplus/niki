#include "niki/l0_core/semantic/extensions.hpp"

namespace niki::semantic {
namespace {
DomainSemanticDeclHandler g_domain_semantic_decl_handler = nullptr;
}

void registerDomainSemanticDeclHandler(DomainSemanticDeclHandler handler) { g_domain_semantic_decl_handler = handler; }

DomainSemanticDeclHandler getDomainSemanticDeclHandler() { return g_domain_semantic_decl_handler; }
} // namespace niki::semantic

