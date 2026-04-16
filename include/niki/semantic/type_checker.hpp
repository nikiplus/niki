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
    std::expected<TypeCheckResult, TypeCheckErrorResult> check(syntax::ASTPool &pool, syntax::ASTNodeIndex root);

  private:
    syntax::ASTPool *currentPool = nullptr;
    std::vector<TypeError> errors;
    NKType currentReturnType = NKType::makeUnknown();

    //---符号表管理---
    struct Symbol {
        uint32_t name_id;
        NKType type;
        int depth;
    };
    std::vector<Symbol> symbols;
    int currentDepth = 0;

    void beginScope() { currentDepth++; }
    void endScope();
    void declareSymbol(uint32_t name_id, NKType type, uint32_t line, uint32_t column);
    NKType resolveSymbol(uint32_t name_id, uint32_t line, uint32_t column);
    //---辅助方法---
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
    NKType checkNode(syntax::ASTNodeIndex nodeIdx);

    //---表达式检查(返回类型并填入typeTable)---
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
    void preDeclareNode(syntax::ASTNodeIndex declIdx);
    void preDeclareFunction(syntax::ASTNodeIndex nodeIdx);

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