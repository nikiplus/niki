#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
#include "niki/l0_core/semantic/module_semantic.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/ast_payloads.hpp"
#include "nktype.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/** @type_checker: 语义一致性与类型约束求解入口
 * 这个头文件定义了语义阶段最关键的“收口器”：TypeChecker。
 * Parser 只能保证语法结构合法，无法回答“这个表达式的类型是什么”“这个名字是否可见”“这个赋值是否允许”。
 * 这些问题必须在语义层统一解决，否则后续 IR 构建只能在不确定前提下工作。
 *
 * 从编译原理看，TypeChecker 做的是“带作用域环境的约束验证”：
 * - 名字解析：标识符要在当前局部作用域、可见符号表或全局符号表中可解析；
 * - 类型约束：二元/一元/调用等表达式要满足操作数和结果类型规则；
 * - 控制流约束：return/break/continue 的使用位置必须合法；
 * - 模块边界：导入可见性与全局预声明结果要一致。
 *
 * 从数据流角度，类型检查并不单纯“返回一个结果对象”，它会把推导类型回填到 `ASTPool.node_types`。
 * 这是一个非常重要的工程决策：后续 IRBuilder 不再重复推导类型，而是消费语义层已经确定的类型标签。
 * 这种“单一事实来源”能显著减少前后阶段类型分歧。
 *
 * 这里之所以显式持有 `GlobalSymbolTable` / `GlobalTypeArena` / `UnitVisibleSymbols`，
 * 是为了把“单文件局部语义”与“项目级全局语义”接起来。
 * 没有这三个上下文，跨文件函数、结构体、导入别名等场景将无法稳定判定。
 *
 * 总结：type_checker.hpp 描述的是语义阶段的核心契约。
 * 它把“语法上能写”过滤成“语义上可成立且可继续编译”，为 IR 层提供可信输入边界。
 */
namespace niki::semantic {

// 检查结果：成功则返回空，失败则返回错误列表
struct TypeCheckResult {
    // 现已统一使用 ASTPool::node_types 存储类型，因此这里无需返回 type_table
};

class TypeChecker {
  public:
    std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> check(syntax::ASTPool &pool,
                                                                          syntax::ASTNodeIndex root);

    // 必须已执行 Driver 级预声明（predeclareSingleUnit / predeclareAllUnits），全局表与 arena 不可为空。
    std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> check(syntax::ASTPool &pool,
                                                                          syntax::ASTNodeIndex root,
                                                                          const GlobalSymbolTable &global_symbols,
                                                                          const GlobalTypeArena &global_arena);
    std::expected<TypeCheckResult, niki::diagnostic::DiagnosticBag> check(
        syntax::ASTPool &pool, syntax::ASTNodeIndex root, const GlobalSymbolTable &global_symbols,
        const GlobalTypeArena &global_arena, const UnitVisibleSymbols &visible_symbols);

  private:
    syntax::ASTPool *currentPool = nullptr;
    niki::diagnostic::DiagnosticBag diagnostics;

    const GlobalSymbolTable *globalSymbols = nullptr;
    const GlobalTypeArena *globalArena = nullptr;
    const UnitVisibleSymbols *visibleSymbols = nullptr;

    NKType currentReturnType = NKType::makeUnknown();
    bool inFunction = false;
    bool inSystemContext = false;

    struct KitsWindowEntry {
        uint32_t component_name_id = 0;
        // true: 默认可写窗口项；false: '&' 前缀只读窗口项
        bool is_mutable = false;
    };
    // kits_name_id -> (alias_name_id -> entry)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, KitsWindowEntry>> kitsWindows;
    // 当前 module 内声明的 component 名称集合（用于 kits 窗口目标合法性校验）。
    std::unordered_set<uint32_t> moduleComponentNames;

    //---符号表管理---
    // 一个名字在当前编译时语义里，最少要记录哪些信息，才能完成类型检查 + 作用域管理 +所有权检查？
    // symbol这个结构体正是为了解决这个问题而存在的
    struct Symbol {
        uint32_t name_id;
        NKType type;
        int depth;
        bool is_owned; // 该符号是否是数据的主人？(决定作用域结束时是否生成 OP_FREE)
        bool is_moved; // 该符号的所有权是否已被转移？(如果被转移，后续严禁使用)
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

    // --- 顶层声明预声明 (两遍扫描的第一遍) ---
    // 先把可前向引用的符号注册到全局作用域，第二遍再检查函数体和声明细节。
    void preDeclareNode(syntax::ASTNodeIndex declIdx);
    void preDeclareFunction(syntax::ASTNodeIndex nodeIdx);
    void preDeclareStruct(syntax::ASTNodeIndex nodeIdx);
    void preDeclareTypeAlias(syntax::ASTNodeIndex nodeIdx);

    // --- 顶层声明 ---
    void checkDeclaration(syntax::ASTNodeIndex declIdx);
    void checkFunctionDecl(syntax::ASTNodeIndex nodeIdx);
    void checkImportDecl(syntax::ASTNodeIndex nodeIdx);
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