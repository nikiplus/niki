#include "niki/l0_core/ir/extensions.hpp"

namespace niki::ir {
namespace {
DomainVerifyAppendFn g_domain_verify_append_fn = nullptr;
}

void registerDomainVerifyAppendFn(DomainVerifyAppendFn fn) { g_domain_verify_append_fn = fn; }
DomainVerifyAppendFn getDomainVerifyAppendFn() { return g_domain_verify_append_fn; }
} // namespace niki::ir

