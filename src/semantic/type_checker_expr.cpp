#include "niki/semantic/nktype.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/token.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace niki::semantic {

NKType TypeChecker::checkExpression(syntax::ASTNodeIndex exprIdx) {
    // 入口：分发到 checkXXXExpr，并将结果类型回填 node_types。
    auto [node, line, column] = getNodeCtx(exprIdx);
    NKType resultType = NKType::makeUnknown();

    switch (node.type) {
    case syntax::NodeType::BinaryExpr:
        resultType = checkBinaryExpr(exprIdx);
        break;
    case syntax::NodeType::LogicalExpr:
        resultType = checkLogicalExpr(exprIdx);
        break;
    case syntax::NodeType::UnaryExpr:
        resultType = checkUnaryExpr(exprIdx);
        break;
    case syntax::NodeType::LiteralExpr:
        resultType = checkLiteralExpr(exprIdx);
        break;
    case syntax::NodeType::IdentifierExpr:
        resultType = checkIdentifierExpr(exprIdx);
        break;
    case syntax::NodeType::ArrayExpr:
        resultType = checkArrayExpr(exprIdx);
        break;
    case syntax::NodeType::MapExpr:
        resultType = checkMapExpr(exprIdx);
        break;
    case syntax::NodeType::IndexExpr:
        resultType = checkIndexExpr(exprIdx);
        break;
    case syntax::NodeType::CallExpr:
        resultType = checkCallExpr(exprIdx);
        break;
    case syntax::NodeType::MemberExpr:
        resultType = checkMemberExpr(exprIdx);
        break;
    case syntax::NodeType::DispatchExpr:
        resultType = checkDispatchExpr(exprIdx);
        break;
    case syntax::NodeType::AwaitExpr:
        resultType = checkAwaitExpr(exprIdx);
        break;
    case syntax::NodeType::BorrowExpr:
        resultType = checkBorrowExpr(exprIdx);
        break;
    case syntax::NodeType::WildcardExpr:
        resultType = checkWildcardExpr(exprIdx);
        break;
    case syntax::NodeType::ImplicitCastExpr:
        resultType = checkImplicitCastExpr(exprIdx);
        break;
    default:
        break;
    }

    // 将推导出的类型记录到 ASTPool 的旁侧表 node_types 中，供 Compiler 阶段查询
    currentPool->node_types[exprIdx.index] = resultType;

    return resultType;
}

// ... [Existing implementations] ...

NKType TypeChecker::checkLiteralExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    switch (node.payload.literal.literal_type) {
    case syntax::TokenType::LITERAL_INT:
        return NKType::makeInt();
    case syntax::TokenType::LITERAL_FLOAT:
        return NKType::makeFloat();
    case syntax::TokenType::KW_TRUE:
    case syntax::TokenType::KW_FALSE:
        return NKType::makeBool();
    case syntax::TokenType::LITERAL_STRING:
        return NKType(NKBaseType::String, -1);
    default:
        return NKType::makeUnknown();
    }
}

NKType TypeChecker::checkIdentifierExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType type = resolveSymbol(node.payload.identifier.name_id, line, column);
    // If not found, it might be a future global or built-in, but for MVP let's return Unknown instead of failing hard.
    return type;
}

NKType TypeChecker::checkBinaryExpr(syntax::ASTNodeIndex nodeIdx) {
    // 二元检查：先求左右类型，再按运算符做约束校验与错误恢复。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    NKType leftType = checkExpression(node.payload.binary.left);
    NKType rightType = checkExpression(node.payload.binary.right);

    if (leftType.getBase() == NKBaseType::Unknown || rightType.getBase() == NKBaseType::Unknown) {
        if (node.payload.binary.op >= syntax::TokenType::SYM_EQUAL_EQUAL &&
            node.payload.binary.op <= syntax::TokenType::SYM_GREATER_EQUAL) {
            return NKType::makeBool();
        }
        return NKType::makeUnknown();
    }

    switch (node.payload.binary.op) {
    case syntax::TokenType::SYM_PLUS:
    case syntax::TokenType::SYM_MINUS:
    case syntax::TokenType::SYM_STAR:
    case syntax::TokenType::SYM_SLASH:
        if (leftType.getBase() == NKBaseType::Integer && rightType.getBase() == NKBaseType::Integer)
            return NKType::makeInt();
        if (leftType.getBase() == NKBaseType::Float && rightType.getBase() == NKBaseType::Float)
            return NKType::makeFloat();

        reportError(line, column, "Arithmetic operations require both Int or both Float.");

        // 启发式错误恢复 (Heuristic Error Recovery)
        // 如果哪怕有一边是浮点数，我们就猜测程序员的意图是浮点运算
        if (leftType.getBase() == NKBaseType::Float || rightType.getBase() == NKBaseType::Float) {
            return NKType::makeFloat();
        }
        // 否则（包括一边是 Int，或者两边都是像 String 这种完全不相干的类型），强行兜底为 Int
        return NKType::makeInt();
    case syntax::TokenType::SYM_MOD:
        if (leftType.getBase() == NKBaseType::Integer && rightType.getBase() == NKBaseType::Integer)
            return NKType::makeInt();
        reportError(line, column, "Modulo operation requires Int.");
        return NKType::makeInt(); // 错误恢复：取模运算必定产生 Int
    case syntax::TokenType::SYM_CONCAT:
        if (leftType.getBase() == NKBaseType::String && rightType.getBase() == NKBaseType::String) {
            return NKType(NKBaseType::String, -1);
        }
        reportError(line, column, "String concatenation '..' requires both operands to be string.");
        return NKType(NKBaseType::String, -1); // 错误恢复：字符串拼接必定产生 String

    case syntax::TokenType::SYM_EQUAL_EQUAL:
    case syntax::TokenType::SYM_BANG_EQUAL:
        if (leftType != rightType) {
            reportError(line, column, "Type mismatch in equality ");
            return NKType::makeBool();
        }
        return NKType::makeBool(); // 补充返回语句
    case niki::syntax::TokenType::SYM_LESS:
    case niki::syntax::TokenType::SYM_LESS_EQUAL:
    case niki::syntax::TokenType::SYM_GREATER:
    case niki::syntax::TokenType::SYM_GREATER_EQUAL:
        if (leftType != rightType ||
            (leftType.getBase() != NKBaseType::Integer && leftType.getBase() != NKBaseType::Float)) {
            reportError(line, column, "Relational operations require matching Int or Float.");
            return NKType::makeBool();
        }
        return NKType::makeBool(); // 补充返回语句
    case niki::syntax::TokenType::SYM_BIT_AND:
    case syntax::TokenType::SYM_BIT_OR: // 补齐位或
    case syntax::TokenType::SYM_BIT_SHL:
    case syntax::TokenType::SYM_BIT_SHR:
    case syntax::TokenType::SYM_BIT_XOR:
        if (leftType.getBase() == NKBaseType::Integer && rightType.getBase() == NKBaseType::Integer) {
            return NKType::makeInt();
        } else {
            reportError(line, column, "Operands must be Int for bitwise operations.");
            return NKType::makeInt(); // 错误恢复：位运算必定产生 Int
        }

    case syntax::TokenType::SYM_DICE: {
        if (node.payload.binary.left.invalid() || node.payload.binary.right.isvalid()) {
            reportError(line, column, "Dice Operations isvalid.");
        }

        if (leftType.getBase() == NKBaseType::Integer && rightType.getBase() == NKBaseType::Integer) {
            return NKType::makeInt();
        } else {
            reportError(line, column, "Operands must be Int for Dice operations.");
            return NKType::makeInt(); // 错误恢复：位运算必定产生 Int
        }
    }
    default:
        reportError(line, column, "Unknown binary operator.");
        return NKType::makeUnknown();
    }
}

NKType TypeChecker::checkArrayExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    auto elements = currentPool->get_list(node.payload.list.elements);
    // 如果是空数组，目前只能返回 Unknown，等待未来的类型推导（比如 var a: Array<Int> = []）
    if (elements.empty()) {
        return NKType(NKBaseType::Array, -1);
    }

    // 获取第一个元素的类型作为基准类型
    NKType elementType = checkExpression(elements[0]);

    // 强制同构检查：后续所有元素的类型必须与第一个元素相同
    for (size_t i = 1; i < elements.size(); ++i) {
        NKType currentType = checkExpression(elements[i]);

        // 如果遇到 Unknown 我们暂时放过，但如果有明确冲突，立刻报错
        if (currentType.getBase() != NKBaseType::Unknown && elementType.getBase() != elementType.getBase()) {
            reportError(line, column,
                        "Heterogeneous arrays are not allowed. Expected element type " +
                            std::to_string((int)elementType.getBase()) + ", got" +
                            std::to_string((int)currentType.getBase()));
        }
    }
    return NKType(NKBaseType::Array, -1);
}

NKType TypeChecker::checkMapExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    const auto &map_data = currentPool->map_data[node.payload.map.map_data_index];
    auto keys = currentPool->get_list(map_data.keys);
    auto values = currentPool->get_list(map_data.values);

    if (keys.empty()) {
        return NKType(NKBaseType::Map, -1);
    }

    NKType keyType = checkExpression(keys[0]);
    NKType valueType = checkExpression(values[0]);

    for (size_t i = 1; i < keys.size(); ++i) {
        NKType curKeyType = checkExpression(keys[i]);
        NKType curValType = checkExpression(values[i]);

        if (curKeyType.getBase() != NKBaseType::Unknown && keyType.getBase() != NKBaseType::Unknown &&
            curKeyType != keyType) {
            reportError(line, column, "Map keys must have uniform type.");
        }
        if (curValType.getBase() != NKBaseType::Unknown && valueType.getBase() != NKBaseType::Unknown &&
            curValType != valueType) {
            reportError(line, column, "Map values must have uniform type.");
        }
    }
    return NKType(NKBaseType::Map, -1);
}

NKType TypeChecker::checkIndexExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType targetType = checkExpression(node.payload.index.target);
    NKType indexType = checkExpression(node.payload.index.index);

    if (targetType.getBase() == NKBaseType::Unknown) {
        return NKType::makeUnknown(); // MVP 兜底
    }

    if (targetType.getBase() != NKBaseType::Array && targetType.getBase() != NKBaseType::Map) {
        reportError(line, column, "Cannot index a non-array/map type.");
        return NKType::makeUnknown();
    }

    // 在完整的泛型系统中，这里应该返回 targetType.elementType 或 targetType.valueType。
    // 但在 MVP 中，因为我们丢弃了泛型参数，我们只能退化返回 Unknown，
    // 让使用者在取回值之后承担弱类型的风险。
    return NKType::makeUnknown();
}

NKType TypeChecker::checkLogicalExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    NKType leftType = checkExpression(node.payload.logical.left);
    NKType rightType = checkExpression(node.payload.logical.right);
    if (leftType.getBase() != NKBaseType::Unknown && leftType.getBase() != NKBaseType::Bool) {
        reportError(line, column, "Left operand of logical expression must be Bool.");
    }
    if (rightType.getBase() != NKBaseType::Unknown && rightType.getBase() != NKBaseType::Bool) {
        reportError(line, column, "Right operand of logical expression must be Bool.");
    }
    return NKType::makeBool();
}

NKType TypeChecker::checkUnaryExpr(syntax::ASTNodeIndex nodeIdx) {
    auto [node, line, column] = getNodeCtx(nodeIdx);
    NKType opType = checkExpression(node.payload.unary.operand);

    switch (node.payload.unary.op) {
    case syntax::TokenType::SYM_MINUS:
        if (opType.getBase() != NKBaseType::Unknown && opType.getBase() != NKBaseType::Integer &&
            opType.getBase() != NKBaseType::Float) {
            reportError(line, column, "Operand for '-' must be Int or Float.");
        }
        return opType; // 返回操作数本身的类型
    case syntax::TokenType::SYM_BANG:
        if (opType.getBase() != NKBaseType::Unknown && opType.getBase() != NKBaseType::Bool &&
            opType.getBase() != NKBaseType::Integer) {
            reportError(line, column, "Operand for '!' must be Bool or Int.");
        }
        return NKType::makeBool();
    case syntax::TokenType::SYM_BIT_NOT:
        if (opType.getBase() != NKBaseType::Unknown && opType.getBase() != NKBaseType::Integer) {
            reportError(line, column, "Operand for '~' must be Int.");
        }
        return NKType::makeInt();
    default:
        return NKType::makeUnknown();
    }
}
NKType TypeChecker::checkCallExpr(syntax::ASTNodeIndex nodeIdx) {
    // 调用检查：区分 Function/Object 两条路径，校验参数个数与类型匹配。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    // 检查被调用的对象。
    NKType calleeType = checkExpression(node.payload.call.callee);

    if (calleeType.getBase() == NKBaseType::Unknown) {
        // 兜底放过
        return NKType::makeUnknown();
    }

    if (calleeType.getBase() != NKBaseType::Function && calleeType.getBase() != NKBaseType::Object) {
        reportError(line, column, "Attempt to call a non-callable value.");
        return NKType::makeUnknown();
    }

    std::span<const syntax::ASTNodeIndex> argNodes = currentPool->get_list(node.payload.call.arguments);

    if (calleeType.getBase() == NKBaseType::Object) {
        // 结构体实例化 (Constructor)
        uint32_t struct_id = calleeType.getTypeId();
        const syntax::StructData &struct_data = currentPool->struct_data[struct_id];
        std::span<const syntax::ASTNodeIndex> fieldTypeNodes = currentPool->get_list(struct_data.types);

        if (argNodes.size() != fieldTypeNodes.size()) {
            reportError(line, column, "Argument count mismatch for struct instantiation.");
        }

        for (size_t i = 0; i < argNodes.size(); ++i) {
            NKType argType = checkExpression(argNodes[i]);
            if (i < fieldTypeNodes.size()) {
                NKType expectedType = resolveTypeAnnotation(fieldTypeNodes[i]);
                if (argType.getBase() != NKBaseType::Unknown && expectedType.getBase() != NKBaseType::Unknown &&
                    argType != expectedType) {
                    reportError(line, column, "Type mismatch in struct argument.");
                }
            }
        }
        return calleeType; // 返回实例类型本身
    }

    // 提取签名进行比对 (Function)
    uint32_t sig_id = static_cast<uint32_t>(calleeType.getTypeId());
    if (globalArena == nullptr) {
        reportError(line, column, "Global type arena is not available.");
        return NKType::makeUnknown();
    }

    const FunctionSignature *sig = globalArena->findFuncSig(sig_id);

    if (sig == nullptr) {
        reportError(line, column, "Invalid function signature id.");
        return NKType::makeUnknown();
    }

    if (argNodes.size() != sig->param_types.size()) {
        reportError(line, column, "Argument count mismatch.");
    }

    // 参数类型校验
    for (size_t i = 0; i < argNodes.size(); ++i) {
        NKType argType = checkExpression(argNodes[i]);
        if (i < sig->param_types.size()) {
            NKType expectedType = sig->param_types[i];
            if (argType.getBase() != NKBaseType::Unknown && expectedType.getBase() != NKBaseType::Unknown &&
                argType != expectedType) {
                reportError(line, column, "Type mismatch in function argument.");
            }
        }
    }
    return sig->return_type;
}

NKType TypeChecker::checkMemberExpr(syntax::ASTNodeIndex nodeIdx) {
    // 成员检查：对象必须是 struct/object，并按字段名回查字段类型。
    auto [node, line, column] = getNodeCtx(nodeIdx);

    NKType objType = checkExpression(node.payload.member.object);
    if (objType.getBase() == NKBaseType::Unknown) {
        return NKType::makeUnknown();
    }

    if (objType.getBase() != NKBaseType::Object) {
        reportError(line, column, "Cannot access member of non-struct type.");
        return NKType::makeUnknown();
    }

    uint32_t struct_id = static_cast<uint32_t>(objType.getTypeId());
    if (globalArena == nullptr) {
        reportError(line, column, "Global type arena is not available.");
        return NKType::makeUnknown();
    }
    const auto *struct_info = globalArena->findStruct(struct_id);
    if (struct_info == nullptr) {
        reportError(line, column, "Invalid global struct type.");
        return NKType::makeUnknown();
    }

    uint32_t target_name_id = node.payload.member.property_id;

    for (size_t i = 0; i < struct_info->field_name_ids.size(); ++i) {
        if (struct_info->field_name_ids[i] == target_name_id) {
            if (i < struct_info->field_types.size()) {
                return struct_info->field_types[i];
            }
            break;
        }
    }

    reportError(line, column, "Struct does not have this field.");
    return NKType::makeUnknown();
}
NKType TypeChecker::checkDispatchExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkAwaitExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkBorrowExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkWildcardExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }
NKType TypeChecker::checkImplicitCastExpr(syntax::ASTNodeIndex nodeIdx) { return NKType::makeUnknown(); }

} // namespace niki::semantic
