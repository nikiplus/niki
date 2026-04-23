#include "niki/debug/logger.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/compiler.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstdint>
#include <span>

namespace niki::syntax {

//---顶层声明预编译 (两遍扫描的第一遍) ---
// 目标：先注册可前向引用的全局符号，再进入第二遍编译其余节点。

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
    // 策略：复用 compileFunctionDecl，使 OP_DEFINE_GLOBAL 先于调用点落盘。
    // 复用 compileFunctionDecl 的逻辑，因为对于 VM 来说，
    // 把 OP_DEFINE_GLOBAL 提前发射到字节码流的开头，就实现了函数提升。
    compileFunctionDecl(nodeIdx);
}

} // namespace niki::syntax