#pragma once

#include "niki/semantic/nktype.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <deque>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vcruntime_typeinfo.h>
#include <vector>

namespace niki::syntax {
// 我们对opcode和token进行抽象，使“符”（token）在这次抽象中成为"意图"(opcode)
// 这里比较难理解，稍微详细的解释可以到虚拟机指北中查看
// 我们可以将ast节点分为如下三个大类
enum class NodeType : uint8_t {
    /*---表达式---*/
    // 计算并产生一个值，也就是说，表达式在执行完毕后，必定会在虚拟机的操作数栈上，留下一个（或多个）确切值的代码片段
    //---基础计算---
    BinaryExpr,     // 二元表达式
    LogicalExpr,    // 逻辑表达式 (&&, || 短路求值)
    UnaryExpr,      // 一元表达式
    LiteralExpr,    // 字面量表达式
    IdentifierExpr, // 标识符表达式
    //---复杂数据结构---
    ArrayExpr, // 数组表达式
    MapExpr,   // 映射表达式
    IndexExpr, // 对象表达式
    //---对象与方法---
    CallExpr,     // 函数调用表达式
    MemberExpr,   // 成员表达式
    DispatchExpr, // 异步派发表达式
    //---闭包与高级特性--
    ClosureExpr,  // 闭包表达式
    AwaitExpr,    // 等待表达式
    BorrowExpr,   // 借用表达式 (&x 或 &mut x)
    WildcardExpr, // 用于 match 语句中的 _
    TypeExpr,     // 类型标注表达式 (如 Int, String, 或者自定义类型)
    //---隐式节点---
    ImplicitCastExpr, // 隐式类型转换表达式
    /*---语句---*/
    // 执行一个动作，但不产生值
    // 所谓语句，就是执行完毕后，虚拟机的操作数栈高度必须与执行前完全一致。
    // 它们的作用是改变环境状态——如声明变量，改变内存，或改变指令流（如跳转）。
    //---基础语句---
    ExpressionStmt, // 表达式语句
    AssignmentStmt, // 赋值语句
    VarDeclStmt,    // 变量声明语句
    ConstDeclStmt,  // 常量声明语句 (复用VarDecl的Payload，避免破坏12字节限制)
    BlockStmt,      // 代码块
    //---控制流---
    IfStmt,        // if语句
    LoopStmt,      // 循环语句
    MatchStmt,     // match语句
    MatchCaseStmt, // match分支语句
    //---跳转与中断---
    ContinueStmt, // continue语句 (零负载节点)
    BreakStmt,    // break语句 (零负载节点)
    ReturnStmt,   // return语句
    NockStmt,     // nock语句
    //---组件挂载与卸载---
    AttachStmt, // 挂载语句
    DetachStmt, // 取消挂载语句
    TargetStmt, // 目标语句 (ECS副作用修改)
    //---异常处理---

    /*---顶层声明---*/
    // 通常只出现在文件的最外层，用于定义数据结构和执行单元
    //---基础声明---
    FunctionDecl,    // 函数声明
    InterfaceMethod, // 接口方法声明
    StructDecl,      // 结构体声明
    EnumDecl,        // 枚举声明
    TypeAliasDecl,   // 类型别名声明
    InterfaceDecl,   // 接口定义
    ImplDecl,        // 实现声明 (impl块)

    //---NIKI特有---
    ModuleDecl,    // 模块声明
    SystemDecl,    // 系统声明
    ComponentDecl, // 组件声明
    FlowDecl,      // 流程声明
    KitsDecl,      // kits
    TagDecl,       // 标签声明
    TagGroupDecl,  // 标签组声明
    //---程序根---
    ProgramRoot, // 程序根节点
    //---错误---
    ErrorNode, // 错误声明
};

inline std::string_view toString(NodeType type) {
    switch (type) {
    case NodeType::BinaryExpr:
        return "BinaryExpr";
    case NodeType::LogicalExpr:
        return "LogicalExpr";
    case NodeType::UnaryExpr:
        return "UnaryExpr";
    case NodeType::LiteralExpr:
        return "LiteralExpr";
    case NodeType::IdentifierExpr:
        return "IdentifierExpr";
    case NodeType::ArrayExpr:
        return "ArrayExpr";
    case NodeType::MapExpr:
        return "MapExpr";
    case NodeType::IndexExpr:
        return "IndexExpr";
    case NodeType::CallExpr:
        return "CallExpr";
    case NodeType::MemberExpr:
        return "MemberExpr";
    case NodeType::DispatchExpr:
        return "DispatchExpr";
    case NodeType::ClosureExpr:
        return "ClosureExpr";
    case NodeType::AwaitExpr:
        return "AwaitExpr";
    case NodeType::BorrowExpr:
        return "BorrowExpr";
    case NodeType::WildcardExpr:
        return "WildcardExpr";
    case NodeType::TypeExpr:
        return "TypeExpr";
    case NodeType::ImplicitCastExpr:
        return "ImplicitCastExpr";
    case NodeType::ExpressionStmt:
        return "ExpressionStmt";
    case NodeType::AssignmentStmt:
        return "AssignmentStmt";
    case NodeType::VarDeclStmt:
        return "VarDeclStmt";
    case NodeType::ConstDeclStmt:
        return "ConstDeclStmt";
    case NodeType::BlockStmt:
        return "BlockStmt";
    case NodeType::IfStmt:
        return "IfStmt";
    case NodeType::LoopStmt:
        return "LoopStmt";
    case NodeType::MatchStmt:
        return "MatchStmt";
    case NodeType::MatchCaseStmt:
        return "MatchCaseStmt";
    case NodeType::ContinueStmt:
        return "ContinueStmt";
    case NodeType::BreakStmt:
        return "BreakStmt";
    case NodeType::ReturnStmt:
        return "ReturnStmt";
    case NodeType::NockStmt:
        return "NockStmt";
    case NodeType::AttachStmt:
        return "AttachStmt";
    case NodeType::DetachStmt:
        return "DetachStmt";
    case NodeType::TargetStmt:
        return "TargetStmt";
    case NodeType::FunctionDecl:
        return "FunctionDecl";
    case NodeType::InterfaceMethod:
        return "InterfaceMethod";
    case NodeType::StructDecl:
        return "StructDecl";
    case NodeType::EnumDecl:
        return "EnumDecl";
    case NodeType::TypeAliasDecl:
        return "TypeAliasDecl";
    case NodeType::InterfaceDecl:
        return "InterfaceDecl";
    case NodeType::ImplDecl:
        return "ImplDecl";
    case NodeType::ModuleDecl:
        return "ModuleDecl";
    case NodeType::SystemDecl:
        return "SystemDecl";
    case NodeType::ComponentDecl:
        return "ComponentDecl";
    case NodeType::FlowDecl:
        return "FlowDecl";
    case NodeType::KitsDecl:
        return "KitsDecl";
    case NodeType::TagDecl:
        return "TagDecl";
    case NodeType::TagGroupDecl:
        return "TagGroupDecl";
    case NodeType::ProgramRoot:
        return "ProgramRoot";
    case NodeType::ErrorNode:
        return "ErrorNode";
    }
    return "UnknownNodeType";
}

/**
 * @brief 【定长节点索引】
 * * 意图：我们设计好每个 ASTNode 的 Payload 都是固定的（比如 16-bit）。
 * 这样只要知道“房号”，就能直接定位到 Node 的物理地址。
     +-------+-------+-------+-------+-------+------
 * * | node0 | node1 | node2 | node3 | node4 | ...
 *   +--↑----+-------+--↑----+-------+-------+------
 *     {0}这里是起始点   {2} ->这就是 ASTNodeIndex(0+ASTNodeIndex)
 */
struct ASTNodeIndex {
    uint32_t index;
    // 为了方便一些不得已而为之的特殊操作，我们留下了一个隐式的转换运算符，使未来我们可以直接将uint32_t视为ASTNodeIndex。
    operator uint32_t() const { return index; }
    bool isvalid() const { return index != ~0U; }
    static ASTNodeIndex invalid() { return {~0U}; }
};

/**
 * @brief 【变长列表索引】
 * * 意图：专门存放数量不定的数据（如：函数参数列表、Block 里的语句）。
 * 我们不只给个起点，而是直接给出“起点 + 长度”，相当于拿到了一个“数据切片”。
 * * 结构区分：
 * - 定长数据（如二元表达式）：直接存 ASTNodeIndex。
 * - 变长数据（如函数参数）：存 ASTListIndex。
 * 这样能保证 std::vector<ASTNode> 里的元素永远规整对齐。

              |<-   length:3  ->|
    ----+-----+-----------------+-----------+-----+-----
     ...| ... |  节点群 A (长度3) |  节点群 B  |节点群|...
     ...|  5  |  6  |  7  |  8  |  9  |  10 |  11 |...
    ----+-----|-----+-----+-----+-----+-----+-----+-----
              ↑start_index=6
    这就是 ASTListIndex{ .start_index = 6, .length = 3 }
 * * * 这样做的好处：
 * 1. 结构对齐：不管 A 群有 3 个还是 300 个节点，存它的“卡片”永远只占 8 字节。
 * 2. 极速切片：在代码里你可以直接通过 data[start] 到 data[start + len] 拿到整组数据。
 */
struct ASTListIndex {
    uint32_t start_index;
    uint32_t length;
    bool isvalid() const { return start_index != ~0U; }
    static ASTListIndex invalid() { return {~0U, 0}; }
};
/* 再次明确：
 * 所有的语法最终在底层都是“数据流”。
 * 将“定长索引”与“变长切片”区分开，能让编译器在处理语法树时，
 * 像收割机处理整齐的麦田一样，最大化发挥现代 CPU 的吞吐能力。
 */
struct FunctionData {
    uint32_t name_id;
    ASTNodeIndex return_type;
    ASTListIndex params;
    ASTNodeIndex body;
};

struct FunctionDeclPayload {
    uint32_t function_index;
};

struct StructData {
    uint32_t name_id;
    ASTListIndex names;
    ASTListIndex types;
};

struct StructDeclPayload {
    uint32_t struct_index;
};

struct ImplData {
    ASTNodeIndex target_type;
    ASTNodeIndex trait_type;
    ASTListIndex methods;
};

struct ImplDeclPayload {
    uint32_t impl_index;
};

struct KitsData {
    uint32_t name_id;
    ASTListIndex members;
};

struct KitsDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
};

struct MapData {
    ASTListIndex keys;
    ASTListIndex values;
};

struct MapExprPayload {
    uint32_t map_data_index;
};

struct BinaryExprPayload {
    TokenType op;
    ASTNodeIndex left;
    ASTNodeIndex right;
};

struct LogicalExprPayload {
    TokenType op;
    ASTNodeIndex left;
    ASTNodeIndex right;
};

struct UnaryExprPayload {
    TokenType op;
    ASTNodeIndex operand;
};

struct LiteralExprPayload {
    TokenType literal_type;
    uint32_t const_pool_index;
};

struct IdentifierExprPayload {
    uint32_t name_id;
};

struct TypeExprPayload {
    TokenType base_type; // 如 KW_INT, KW_FLOAT，如果是自定义类型可以用 IDENTIFIER
    uint32_t name_id;    // 如果是自定义类型，存储其字符串ID；如果是内置类型，可以为0
};

struct IndexExprPayload {
    ASTNodeIndex target;
    ASTNodeIndex index;
};

struct CallExprPayload {
    ASTNodeIndex callee;
    ASTListIndex arguments;
};

struct MemberExprPayload {
    ASTNodeIndex object;
    uint32_t property_id;
};

struct DispatchExprPayload {
    ASTNodeIndex callee;
    ASTListIndex arguments;
};

struct ClosureExprPayload {
    ASTNodeIndex function_decl;
    ASTListIndex captures;
};

struct AwaitExprPayload {
    ASTNodeIndex operand;
    ASTNodeIndex kits;
};

struct BorrowExprPayload {
    bool is_mut;
    ASTNodeIndex operand;
};

struct ImplicitCastExprPayload {
    ASTNodeIndex operand;
    semantic::NKType to_type;
};

struct ExpressionStmtPayload {
    ASTNodeIndex expression;
};

struct AssignmentStmtPayload {
    TokenType op;
    ASTNodeIndex target;
    ASTNodeIndex value;
};

struct VarDeclStmtPayload {
    uint32_t name_id;
    ASTNodeIndex type_expr;
    ASTNodeIndex init_expr;
};

struct IfStmtPayload {
    ASTNodeIndex condition;
    ASTNodeIndex then_branch;
    ASTNodeIndex else_branch;
};

struct LoopStmtPayload {
    ASTNodeIndex condition;
    ASTNodeIndex body;
};

struct MatchStmtPayload {
    ASTNodeIndex expression;
    ASTListIndex cases;
};

struct MatchCaseStmtPayload {
    ASTListIndex pattern;
    ASTNodeIndex body;
};

struct ReturnStmtPayload {
    ASTNodeIndex expression;
};

struct NockStmtPayload {
    ASTNodeIndex interval;
};

struct AttachStmtPayload {
    ASTNodeIndex component;
    ASTNodeIndex target;
};

struct DetachStmtPayload {
    ASTNodeIndex component;
    ASTNodeIndex target;
};

struct TargetStmtPayload {
    ASTNodeIndex targets;
    ASTNodeIndex body;
};

struct EnumDeclPayload {
    ASTNodeIndex enum_data;
};

struct TypeAliasDeclPayload {
    uint32_t name_id;
    ASTNodeIndex type_expr;
};

struct InterfaceDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
};

struct ModuleDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
};

struct SystemDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
    ASTNodeIndex system_data;
};

struct ComponentDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
};

struct FlowDeclPayload {
    uint32_t name_id;
    ASTNodeIndex body;
};

struct TagDeclPayload {
    uint32_t name_id;
};

struct TagGroupDeclPayload {
    uint32_t name_id;
    ASTListIndex tags;
};

struct ListPayload {
    ASTListIndex elements;
};

struct ErrorPayload {
    uint32_t error_index;
};

} // namespace niki::syntax