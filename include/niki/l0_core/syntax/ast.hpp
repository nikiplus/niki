#pragma once

#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vcruntime_typeinfo.h>
#include <vector>


namespace niki::syntax {

// ASTNodeIndex / ASTListIndex 等“索引而非指针”的动机、旁侧表与 locations 对齐关系：
// 见 src/syntax/ast.cpp 文件顶部「AST 与 ASTPool 设计说明」。

union ASTNodePayload {
    // --- [syntax.expr] ---
    BinaryExprPayload binary;
    LogicalExprPayload logical;
    UnaryExprPayload unary;
    LiteralExprPayload literal;
    IdentifierExprPayload identifier;
    // --- [syntax.expr] 复合字面量 / 类型字面量（Array 复用 list）---
    TypeExprPayload type_expr;
    MapExprPayload map;
    IndexExprPayload index;
    CallExprPayload call;
    MemberExprPayload member;
    DispatchExprPayload dispatch;
    AwaitExprPayload await_expr;
    BorrowExprPayload borrow;
    ImplicitCastExprPayload implicit_cast;

    // --- [syntax.stmt] ---
    TargetStmtPayload target_stmt;
    ExpressionStmtPayload expr_stmt;
    AssignmentStmtPayload assign_stmt;
    VarDeclStmtPayload var_decl;
    // BlockStmtPayload 合并至 ListPayload
    IfStmtPayload if_stmt;
    LoopStmtPayload loop;
    MatchStmtPayload match_stmt;
    MatchCaseStmtPayload match_case;
    ReturnStmtPayload return_stmt;
    NockStmtPayload nock;
    AttachStmtPayload attach;
    DetachStmtPayload detach;

    // --- [syntax.decl] 顶层与块内声明 ---
    FunctionDeclPayload func_decl;
    // InterfaceMethodPayload 合并至 FunctionDeclPayload
    StructDeclPayload struct_decl;
    EnumDeclPayload enum_decl;
    TypeAliasDeclPayload type_alias;
    InterfaceDeclPayload interface_decl;
    ImplDeclPayload impl_decl;
    ModuleDeclPayload module_decl;
    SystemDeclPayload system_decl;
    ComponentDeclPayload component_decl;
    FlowDeclPayload flow_decl;
    KitsDeclPayload kits_decl;
    TagDeclPayload tag_decl;
    TagGroupDeclPayload tag_group;

    // --- [syntax.core] 通用列表载体（Block、ProgramRoot、数组字面量等）---
    ListPayload list;

    // --- [syntax] 解析错误占位 ---
    ErrorPayload error;
};

// [syntax] 单个语法节点：type + payload（定长 16 字节，变长数据走 lists_elements 与各类旁侧表）。
struct ASTNode {
    NodeType type;
    ASTNodePayload payload;
};

// [syntax.diagnostic] 源码位置（与 nodes 按下标一一对齐）。
struct TokenLocation {
    uint32_t line;
    uint32_t column;
};

// [syntax + semantic + vm] 整棵 AST 与旁侧数据的唯一持有者（解析 / 语义 / 编译共用）。
// 字段按「所属阶段」分组；旁侧表与索引对齐原理见 ast.cpp 顶部说明。
struct ASTPool {
    std::string source_path;
    // --- [syntax.intern] 内置类型名在 string_pool 中的固定 ID（构造期注入）---
    uint32_t ID_INT = 0;
    uint32_t ID_FLOAT = 1;
    uint32_t ID_BOOL = 2;
    uint32_t ID_STRING = 3;

    // --- [syntax.core] 主节点表 + 变长子节点索引扁平区（列表切片由 ASTListIndex 指向此区）---
    std::vector<ASTNode> nodes;
    std::vector<ASTNodeIndex> lists_elements;

    // --- [syntax.diagnostic] 与 nodes 同下标：行/列（报错、编译回溯）---
    std::vector<TokenLocation> locations;

    // --- [semantic] 与 nodes 同下标：表达式/子表达式的静态类型（TypeChecker 写，Compiler 读）---
    std::vector<semantic::NKType> node_types;

    // --- [syntax + vm] 解析期字面量常量池；AST 内仅存 const_pool_index，避免把 vm::Value 塞进定长节点 ---
    std::vector<vm::Value> constants;

    // --- [syntax.decl] 声明级旁侧表（节点 payload 里只存「本声明在下列表中的下标」）---
    std::vector<FunctionData> function_data;
    std::vector<StructData> struct_data;
    std::vector<ImplData> impl_data;
    std::vector<KitsData> kits_data;
    std::vector<MapData> map_data;

    // --- [semantic] 函数签名去重池（类型检查 intern，NKType::Function 携带 sig 下标）---
    std::vector<semantic::FunctionSignature> func_sigs;
    uint32_t internFuncSignature(const std::vector<semantic::NKType> &params, semantic::NKType retType) {
        semantic::FunctionSignature sig{params, retType};
        for (size_t i = 0; i < func_sigs.size(); ++i) {
            if (func_sigs[i] == sig) {
                return i;
            }
        }
        func_sigs.push_back(sig);
        return func_sigs.size() - 1;
    }

    // --- [syntax.intern] Driver 级共享字符串驻留表（ASTPool 只转发，不持有权威ID状态）---
    GlobalInterner *interner = nullptr;

    // --- [syntax.core] 列表视图：对 lists_elements 的只读切片（实现与说明见 ast.cpp）---
    std::span<const ASTNodeIndex> get_list(ASTListIndex list_info) const;

    explicit ASTPool(GlobalInterner &shared_interner);

    ASTNodeIndex allocateNode(NodeType type);
    ASTListIndex allocateList(std::span<const ASTNodeIndex> elements);

    // --- [syntax + semantic] 辅助 ---
    uint32_t addConstant(vm::Value value);
    void clear();
    ASTNode &getNode(ASTNodeIndex index);
    const ASTNode &getNode(ASTNodeIndex index) const;
    uint32_t internString(std::string_view str);
    const std::string &getStringId(uint32_t id) const;
    std::vector<std::string> snapshotStringPool() const;

    // --- Map Data Allocation ---
    uint32_t addMapData(const std::vector<ASTNodeIndex> &interleaved) {
        std::vector<ASTNodeIndex> keys;
        std::vector<ASTNodeIndex> values;
        keys.reserve(interleaved.size() / 2);
        values.reserve(interleaved.size() / 2);
        for (size_t i = 0; i < interleaved.size(); i += 2) {
            keys.push_back(interleaved[i]);
            values.push_back(interleaved[i + 1]);
        }

        MapData data;
        data.keys = allocateList(keys);
        data.values = allocateList(values);

        map_data.push_back(data);
        return static_cast<uint32_t>(map_data.size() - 1);
    }
};

} // namespace niki::syntax