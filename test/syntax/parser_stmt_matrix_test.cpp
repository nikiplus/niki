#include "../helpers/parse_test_helpers.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax;

/** @parser_stmt_matrix: 语句 Parser 逐条矩阵（ID S-xx） */

TEST(ParserMatrixStmt, S01_VarDecl) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: int = 1; return x;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::VarDeclStmt, 1);
}

TEST(ParserMatrixStmt, S02_ConstDecl) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("const x: int = 1; return x;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ConstDeclStmt, 1);
}

TEST(ParserMatrixStmt, S03_IfWithoutElse) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("if (true) { return 1; } return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::IfStmt, 1);
}

TEST(ParserMatrixStmt, S04_IfWithElse) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("if (true) { return 1; } else { return 0; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::IfStmt, 1);
}

TEST(ParserMatrixStmt, S05_LoopWithCondition) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var i: int = 0; loop (i < 3) { i = i + 1; } return i;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LoopStmt, 1);
}

TEST(ParserMatrixStmt, S06_BreakInLoop) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("loop (true) { break; } return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BreakStmt, 1);
}

TEST(ParserMatrixStmt, S07_ContinueInLoop) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("loop (true) { continue; } return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ContinueStmt, 1);
}

TEST(ParserMatrixStmt, S08_ReturnStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 42;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ReturnStmt, 1);
}

TEST(ParserMatrixStmt, S09_MatchStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody(
        "var x: int = 1; match (x) { case 1 => { return 0; } } return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::MatchStmt, 1);
    f.expectNodeCount(unit.pool, NodeType::MatchCaseStmt, 1);
}

TEST(ParserMatrixStmt, S10_ExpressionStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("1 + 2; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ExpressionStmt, 1);
}

TEST(ParserMatrixStmt, S11_AssignmentStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: int = 0; x = 5; return x;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::AssignmentStmt, 1);
}

TEST(ParserMatrixStmt, S12_NockBare) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("nock; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::NockStmt, 1);
}

TEST(ParserMatrixStmt, S13_NockWithInterval) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("nock 5; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::NockStmt, 1);
}

TEST(ParserMatrixStmt, S14_AttachStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("attach C to t; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::AttachStmt, 1);
}

TEST(ParserMatrixStmt, S15_DetachStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("detach C from t; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::DetachStmt, 1);
}

TEST(ParserMatrixStmt, S16_BlockStmt) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("{ var x: int = 1; return x; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BlockStmt, 1);
}

TEST(ParserMatrixStmt, S17_ElseIfChain) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("if (false) { return 1; } else if (true) { return 2; } else { return 3; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::IfStmt, 2);
}
