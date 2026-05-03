#include "../test_helpers.hpp"
#include "ast_printer.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax::test;
using namespace niki::syntax;

/** @phase_S: 语句解析测试
 *
 * 验证 parser_statement.cpp 中各语句节点的解析正确性。
 * 测试用例使用完整的 module → func → body 包裹链路（ExprTestFixture）。
 */

// S-1: If-Else（无 else 分支）
TEST(ParserStmtTest, IfWithoutElse) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "if (true) { return 1; } return 0;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto if_nodes = fixture.findNodes(unit.pool, NodeType::IfStmt);
    EXPECT_GE(if_nodes.size(), 1u);
}

// S-2: If-Else（有 else 分支）
TEST(ParserStmtTest, IfWithElse) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "if (true) { return 1; } else { return 2; }"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto if_nodes = fixture.findNodes(unit.pool, NodeType::IfStmt);
    EXPECT_GE(if_nodes.size(), 1u);
}

// S-3: Loop（有条件）
TEST(ParserStmtTest, LoopWithCondition) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 0; loop (x < 10) { x = x + 1; } return x;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto loop_nodes = fixture.findNodes(unit.pool, NodeType::LoopStmt);
    EXPECT_GE(loop_nodes.size(), 1u);
}

// S-4: Loop（无条件）+ Break
TEST(ParserStmtTest, LoopWithBreak) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 0; loop { if (x > 5) { break; } x = x + 1; } return x;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto loop_nodes = fixture.findNodes(unit.pool, NodeType::LoopStmt);
    EXPECT_GE(loop_nodes.size(), 1u);
    auto break_nodes = fixture.findNodes(unit.pool, NodeType::BreakStmt);
    EXPECT_GE(break_nodes.size(), 1u);
}

// S-5: Continue 语句
TEST(ParserStmtTest, ContinueStmt) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 0; loop (x < 5) { x = x + 1; if (x < 3) { continue; } break; } return x;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto continue_nodes = fixture.findNodes(unit.pool, NodeType::ContinueStmt);
    EXPECT_GE(continue_nodes.size(), 1u);
    auto break_nodes = fixture.findNodes(unit.pool, NodeType::BreakStmt);
    EXPECT_GE(break_nodes.size(), 1u);
}

// S-6: Match 语句（基础模式）
TEST(ParserStmtTest, MatchStmt) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 1; match (x) { case 1 => { return 10; } case 2 => { return 20; } } return 0;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto match_nodes = fixture.findNodes(unit.pool, NodeType::MatchStmt);
    EXPECT_GE(match_nodes.size(), 1u);
    auto case_nodes = fixture.findNodes(unit.pool, NodeType::MatchCaseStmt);
    EXPECT_GE(case_nodes.size(), 1u);
}

// S-7: 赋值语句（=）
TEST(ParserStmtTest, AssignmentStmt) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 10; x = x + 1; return x;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto assign_nodes = fixture.findNodes(unit.pool, NodeType::AssignmentStmt);
    EXPECT_GE(assign_nodes.size(), 1u);
}

// S-8: Nock 语句
TEST(ParserStmtTest, NockStmt) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "nock; return 42;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto nock_nodes = fixture.findNodes(unit.pool, NodeType::NockStmt);
    EXPECT_GE(nock_nodes.size(), 1u);
}
