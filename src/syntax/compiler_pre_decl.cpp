#include "niki/debug/logger.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <span>

namespace niki::syntax {

//---顶层声明预编译 (两遍扫描的第一遍) ---

void Compiler::preCompileNode(ASTNodeIndex nodeIdx) {
    if (!nodeIdx.isvalid())
        return;
    const auto &node = getNodeCtx(nodeIdx).node;
    if (node.type == NodeType::FunctionDecl) {
        preCompileFunctionDecl(nodeIdx);
    } else if (node.type == NodeType::StructDecl) {
        compileStructDecl(nodeIdx);
    }
}

void Compiler::preCompileFunctionDecl(ASTNodeIndex nodeIdx) {
    // 复用 compileFunctionDecl 的逻辑，因为对于 VM 来说，
    // 把 OP_DEFINE_GLOBAL 提前发射到字节码流的开头，就实现了函数提升。
    compileFunctionDecl(nodeIdx);
}

} // namespace niki::syntax