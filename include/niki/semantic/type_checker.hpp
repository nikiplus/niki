#pragma once
#include "niki/semantic/nktype.hpp"
#include "niki/syntax/ast.hpp"
#include "nktype.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace niki::semantic {

// 类型检查错误记录
struct TypeError {
    uint32_t line;
    uint32_t column;
    std::string message;
};
// 检查结果：成功则返回空，失败则返回错误列表
struct TypeCheckResult {
    // 现已统一使用 ASTPool::node_types 存储类型，因此这里无需返回 type_table
};

struct TypeCheckErrorResult {
    std::vector<TypeError> errors;
};

class TypeChecker {
  public:
    // 注意：这里的 pool 不再是 const，因为我们需要写入 pool.node_types
    // 类型检查总入口：
    // 1) 绑定 ASTPool 与内部状态
    // 2) 深度遍历并对表达式/语句/声明做静态检查
    // 3) 将表达式结果类型写回 pool.node_types
    std::expected<TypeCheckResult, TypeCheckErrorResult> check(syntax::ASTPool &pool, syntax::ASTNodeIndex root);

  private:
    syntax::ASTPool *currentPool = nullptr;
    std::vector<TypeError> errors;
    NKType currentReturnType = NKType::makeUnknown();
    bool inFunction = false;

    //---符号表管理---
    // 一个名字在当前编译时语义里，最少要记录哪些信息，才能完成类型检查 + 作用域管理 +（将来）所有权检查？
    //symbol这个结构体正是为了解决这个问题而存在的
    struct Symbol {
        uint32_t name_id;
        NKType type;
        int depth;
        bool is_owned; // 灵魂拷问1：该符号是否是数据的主人？(决定作用域结束时是否生成 OP_FREE)
        bool is_moved; // 灵魂拷问2：该符号的所有权是否已被转移？(如果被转移，后续严禁使用)
    };
    std::vector<Symbol> symbols;
    int currentDepth = 0;

    void beginScope() { currentDepth++; }
    void endScope();
    void declareSymbol(uint32_t name_id, NKType type, uint32_t line, uint32_t column, bool is_owned = false);
    NKType resolveSymbol(uint32_t name_id, uint32_t line, uint32_t column);
    //---辅助方法---
    // getNodeCtx 统一拉取 node + 源码位置信息，避免各处手动访问旁侧表。
    struct NodeContext {
        const syntax::ASTNode &node;
        uint32_t line;
        uint32_t column;
    };
    inline NodeContext getNodeCtx(syntax::ASTNodeIndex idx) const {
        return {currentPool->getNode(idx), currentPool->locations[idx.index].line,
                currentPool->locations[idx.index].column};
    }

    NKType resolveTypeAnnotation(syntax::ASTNodeIndex typeNodeIdx);
    //---错误报告---
    void reportError(uint32_t line, uint32_t column, const std::string &message);

    //---遍历入口---
    // checkNode 是语义层总分发器，根据 NodeType 路由到表达式/语句/声明分支。
    NKType checkNode(syntax::ASTNodeIndex nodeIdx);

    //---表达式检查(返回类型并填入typeTable)---
    // 所有 checkXXXExpr 必须返回一个 NKType，并在 checkExpression 中回填 node_types。
    NKType checkExpression(syntax::ASTNodeIndex exprIdx);
    NKType checkBinaryExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkLogicalExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkUnaryExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkLiteralExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkIdentifierExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkArrayExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkMapExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkIndexExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkCallExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkMemberExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkDispatchExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkAwaitExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkBorrowExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkWildcardExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkImplicitCastExpr(syntax::ASTNodeIndex nodeIdx);

    // --- 语句检查 (不返回类型，只做合法性校验和符号表操作) ---
    // 语句分支负责作用域管理与控制流合法性，不直接向调用方暴露类型结果。
    void checkStatement(syntax::ASTNodeIndex stmtIdx);
    void checkExpressionStmt(syntax::ASTNodeIndex nodeIdx);
    void checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx);
    void checkVarDeclStmt(syntax::ASTNodeIndex nodeIdx);
    void checkConstDeclStmt(syntax::ASTNodeIndex nodeIdx);
    void checkBlockStmt(syntax::ASTNodeIndex nodeIdx);
    void checkIfStmt(syntax::ASTNodeIndex nodeIdx);
    void checkLoopStmt(syntax::ASTNodeIndex nodeIdx);
    void checkMatchStmt(syntax::ASTNodeIndex nodeIdx);
    void checkMatchCaseStmt(syntax::ASTNodeIndex nodeIdx);
    void checkContinueStmt(syntax::ASTNodeIndex nodeIdx);
    void checkBreakStmt(syntax::ASTNodeIndex nodeIdx);
    void checkReturnStmt(syntax::ASTNodeIndex nodeIdx);
    void checkNockStmt(syntax::ASTNodeIndex nodeIdx);
    void checkAttachStmt(syntax::ASTNodeIndex nodeIdx);
    void checkDetachStmt(syntax::ASTNodeIndex nodeIdx);
    void checkTargetStmt(syntax::ASTNodeIndex nodeIdx);

    // --- 顶层声明预声明 (两遍扫描的第一遍) ---
    // 先把可前向引用的符号注册到全局作用域，第二遍再检查函数体和声明细节。
    void preDeclareNode(syntax::ASTNodeIndex declIdx);
    void preDeclareFunction(syntax::ASTNodeIndex nodeIdx);
    void preDeclareStruct(syntax::ASTNodeIndex nodeIdx);

    // --- 顶层声明 ---
    void checkDeclaration(syntax::ASTNodeIndex declIdx);
    void checkFunctionDecl(syntax::ASTNodeIndex nodeIdx);
    void checkInterfaceMethod(syntax::ASTNodeIndex nodeIdx);
    void checkStructDecl(syntax::ASTNodeIndex nodeIdx);
    void checkEnumDecl(syntax::ASTNodeIndex nodeIdx);
    void checkTypeAliasDecl(syntax::ASTNodeIndex nodeIdx);
    void checkInterfaceDecl(syntax::ASTNodeIndex nodeIdx);
    void checkImplDecl(syntax::ASTNodeIndex nodeIdx);
    void checkModuleDecl(syntax::ASTNodeIndex nodeIdx);
    void checkSystemDecl(syntax::ASTNodeIndex nodeIdx);
    void checkComponentDecl(syntax::ASTNodeIndex nodeIdx);
    void checkFlowDecl(syntax::ASTNodeIndex nodeIdx);
    void checkKitsDecl(syntax::ASTNodeIndex nodeIdx);
    void checkTagDecl(syntax::ASTNodeIndex nodeIdx);
    void checkTagGroupDecl(syntax::ASTNodeIndex nodeIdx);
    void checkProgramRoot(syntax::ASTNodeIndex nodeIdx);
};

} // namespace niki::semantic