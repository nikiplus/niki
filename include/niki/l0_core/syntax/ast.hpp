#pragma once

#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "niki/l0_core/syntax/global_interner.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
    ImportDeclPayload import_decl;
    ExportDeclPayload export_decl;
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
    NodeType type;         ///< 节点类型标签。
    ASTNodePayload payload; ///< 节点载荷（定长 union）。
};

// [syntax.diagnostic] 源码位置（与 nodes 按下标一一对齐）。
struct TokenLocation {
    uint32_t line;   ///< 行号（1-based）。
    uint32_t column; ///< 列号（1-based）。
};

// [syntax + semantic + vm] 整棵 AST 与旁侧数据的唯一持有者（解析 / 语义 / 编译共用）。
// 字段按「所属阶段」分组；旁侧表与索引对齐原理见 ast.cpp 顶部说明。
struct ASTPool {
    std::string source_path; ///< 源文件路径（诊断与回溯使用）。
    // --- [syntax.intern] 内置类型名在 string_pool 中的固定 ID（构造期注入）---
    uint32_t ID_INT = 0;
    uint32_t ID_FLOAT = 1;
    uint32_t ID_BOOL = 2;
    uint32_t ID_STRING = 3;

    // --- [syntax.core] 主节点表 + 变长子节点索引扁平区（列表切片由 ASTListIndex 指向此区）---
    std::vector<ASTNode> nodes; ///< AST 主节点表。
    std::vector<ASTNodeIndex> lists_elements; ///< 变长列表扁平存储区。

    // --- [syntax.diagnostic] 与 nodes 同下标：行/列（报错、编译回溯）---
    std::vector<TokenLocation> locations; ///< 与 nodes 同下标位置表。

    // --- [semantic] 与 nodes 同下标：表达式/子表达式的静态类型（TypeChecker 写，IRBuilder 读）---
    std::vector<semantic::NKType> node_types; ///< 与 nodes 同下标类型表。

    // --- [syntax + vm] 解析期字面量常量池；AST 内仅存 const_pool_index，避免把 vm::Value 塞进定长节点 ---
    std::vector<vm::Value> constants; ///< 字面量常量池。

    // --- [syntax.decl] 声明级旁侧表（节点 payload 里只存「本声明在下列表中的下标」）---
    std::vector<FunctionData> function_data; ///< 函数声明旁侧表。
    std::vector<StructData> struct_data; ///< 结构体声明旁侧表。
    std::vector<ImplData> impl_data; ///< impl 声明旁侧表。
    std::vector<KitsData> kits_data; ///< kits 声明旁侧表。
    std::vector<MapData> map_data; ///< map 字面量旁侧表。
    std::vector<ImportItem> import_items; ///< import 条目池。
    std::vector<ExportItem> export_items; ///< export 条目池。
    std::vector<ImportDeclData> import_decl_data; ///< import 声明旁侧表。
    std::vector<ExportDeclData> export_decl_data; ///< export 声明旁侧表。

    // 函数签名的权威 intern 在 GlobalTypeArena；NKType::Function 的 type_id 均为全局 sig id。

    // --- [syntax.intern] Driver 级共享字符串驻留表（ASTPool 只转发，不持有权威ID状态）---
    GlobalInterner *interner = nullptr; ///< Driver 共享 interner（不拥有生命周期）。

    // --- [syntax.core] 列表视图：对 lists_elements 的只读切片（实现与说明见 ast.cpp）---
    /** @brief 获取 ASTListIndex 对应的只读列表切片。 */
    std::span<const ASTNodeIndex> get_list(ASTListIndex list_info) const;

    /** @brief 构造 ASTPool 并绑定共享 interner。 */
    explicit ASTPool(GlobalInterner &shared_interner);

    /** @brief 分配空节点并同步追加旁侧表项。 */
    ASTNodeIndex allocateNode(NodeType type);
    /** @brief 将列表元素写入扁平区并返回切片索引。 */
    ASTListIndex allocateList(std::span<const ASTNodeIndex> elements);

    // --- [syntax + semantic] 辅助 ---
    /** @brief 追加常量并返回常量池索引。 */
    uint32_t addConstant(vm::Value value);
    /** @brief 清空 AST 结构数据（不重置共享 interner）。 */
    void clear();
    /** @brief 读取可写节点引用。 */
    ASTNode &getNode(ASTNodeIndex index);
    /** @brief 读取只读节点引用。 */
    const ASTNode &getNode(ASTNodeIndex index) const;
    /** @brief 驻留字符串到共享 interner。 */
    uint32_t internString(std::string_view str);
    /** @brief 由 id 反查字符串。 */
    const std::string &getStringId(uint32_t id) const;
    /** @brief 导出当前字符串池快照。 */
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