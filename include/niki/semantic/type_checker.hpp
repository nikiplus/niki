#pragma once
#include "niki/semantic/nktype.hpp"
#include "niki/syntax/ast.hpp"
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
// 检查结果：成功则返回类型表，失败则返回错误列表
struct TypeCheckResult {
    std::vector<semantic::NKType> type_table; // 与 ASTPool.nodes 一一对应
};

struct TypeCheckErrorResult {
    std::vector<TypeError> errors;
};

class TypeChecker {
  public:
    std::expected<TypeCheckResult, TypeCheckErrorResult> check(const syntax::ASTPool &pool, syntax::ASTNodeIndex root);

  private:
    const syntax::ASTPool *currentPool = nullptr;
    std::vector<TypeError> errors;
    std::vector<NKType> typeTable; // 正在构建的类型表

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

    //---错误报告---
    void reportError(uint32_t line, uint32_t column, const std::string &message);

    //---遍历入口---
    NKType checkNode(syntax::ASTNodeIndex nodeIdx);

    //---表达式检查(返回类型并填入typeTable)---
    NKType checkExpression(syntax::ASTNodeIndex exprIdx);
    NKType checkLiteralExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkIdentifierExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkBinaryExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkArrayExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkMapExpr(syntax::ASTNodeIndex nodeIdx);
    NKType checkIndexExpr(syntax::ASTNodeIndex nodeIdx); // <--- 解决 Array vs Map 的核心！

    // --- 语句检查 (不返回类型，只做合法性校验和符号表操作) ---
    void checkStatement(syntax::ASTNodeIndex stmtIdx);
    void checkVarDeclStmt(syntax::ASTNodeIndex nodeIdx);
    void checkAssignmentStmt(syntax::ASTNodeIndex nodeIdx);
    void checkBlockStmt(syntax::ASTNodeIndex nodeIdx);
    void checkIfStmt(syntax::ASTNodeIndex nodeIdx);

    // --- 顶层声明 ---
    void checkDeclaration(syntax::ASTNodeIndex declIdx);
    void checkModuleDecl(syntax::ASTNodeIndex nodeIdx);
};

} // namespace niki::semantic