#include "../helpers/parse_test_helpers.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax;

/** @parser_expr_matrix: 表达式 Parser 逐条矩阵（ID E-xx） */

TEST(ParserMatrixExpr, E01_IntegerLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: int = 42; return x;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E02_FloatLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: float = 3.14; return x;", "float");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E03_StringLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: string = \"hi\"; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E04_BoolLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: bool = true; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E05_NilLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return nil;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E06_CharLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 'a';");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LiteralExpr, 1);
}

TEST(ParserMatrixExpr, E07_UnaryMinus) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return -5;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::UnaryExpr, 1);
}

TEST(ParserMatrixExpr, E08_UnaryNot) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return !true;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::UnaryExpr, 1);
}

TEST(ParserMatrixExpr, E09_UnaryBitNot) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return ~7;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::UnaryExpr, 1);
}

TEST(ParserMatrixExpr, E10_ArithmeticAdd) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 1 + 2;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 1);
}

TEST(ParserMatrixExpr, E11_ArithmeticSubMulDivMod) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 10 - 2 * 3 / 2 % 2;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 4);
}

TEST(ParserMatrixExpr, E12_ComparisonOps) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 1 == 2 && 3 != 4 && 1 < 2 && 2 <= 3 && 3 > 1 && 3 >= 3;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 6);
    f.expectNodeCount(unit.pool, NodeType::LogicalExpr, 5);
}

TEST(ParserMatrixExpr, E13_LogicalAndOr) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return true && false || true;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::LogicalExpr, 2);
}

TEST(ParserMatrixExpr, E14_BitwiseOps) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 1 & 2 | 3 ^ 4 << 1 >> 1;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 5);
}

TEST(ParserMatrixExpr, E15_StringConcat) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var s: string = \"a\" .. \"b\"; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 1);
}

TEST(ParserMatrixExpr, E16_DiceExpression) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return 1d6 + 0d100;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 3);
}

TEST(ParserMatrixExpr, E17_ParenthesisGrouping) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return (1 + 2) * 3;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::BinaryExpr, 2);
}

TEST(ParserMatrixExpr, E18_ArrayLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return [1, 2, 3];");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ArrayExpr, 1);
}

TEST(ParserMatrixExpr, E19_IndexAccess) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var a = [1,2]; return a[0];");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::IndexExpr, 1);
}

TEST(ParserMatrixExpr, E20_MapLiteral) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return {1: 2, 3: 4};");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::MapExpr, 1);
}

TEST(ParserMatrixExpr, E21_FunctionCall) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("func f(x: int) -> int { return x; } func main() -> int { return f(1); }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::CallExpr, 1);
}

TEST(ParserMatrixExpr, E22_MemberAccess) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls(
        "struct P { x: int } func main() -> int { var p: P = P(0); return p.x; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::MemberExpr, 1);
}

TEST(ParserMatrixExpr, E23_TypeExprKeyword) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: int = 0; return 0;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::TypeExpr, 1);
}

TEST(ParserMatrixExpr, E24_AwaitExprNotInParser) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return await 1;");
    f.expectNodeAbsent(unit.pool, NodeType::AwaitExpr);
}

TEST(ParserMatrixExpr, E25_BorrowExprNotInParser) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return &x;");
    f.expectNodeAbsent(unit.pool, NodeType::BorrowExpr);
}

TEST(ParserMatrixExpr, E26_DispatchExprNotInParser) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("return foo::bar;");
    f.expectNodeAbsent(unit.pool, NodeType::DispatchExpr);
}

TEST(ParserMatrixExpr, E27_IdentifierReference) {
    ParseTestFixture f;
    auto unit = f.parseStmtBody("var x: int = 1; return x;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::IdentifierExpr, 1);
}
