#include "../helpers/parse_test_helpers.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax;

/** @parser_decl_independent: 顶层声明 Parser 矩阵（无表达式体依赖，ID D-01～D-11） */

TEST(ParserMatrixDeclInd, D01_FunctionDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("func foo(x: int) -> int { return x; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::FunctionDecl, 1);
}

TEST(ParserMatrixDeclInd, D02_StructDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("struct Point { x: int, y: int }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::StructDecl, 1);
}

TEST(ParserMatrixDeclInd, D03_EnumDeclStub) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("enum Color { Red, Green }");
    f.expectNodeAbsent(unit.pool, NodeType::EnumDecl);
}

TEST(ParserMatrixDeclInd, D04_TypeAliasDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("type Alias = int;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::TypeAliasDecl, 1);
}

TEST(ParserMatrixDeclInd, D05_InterfaceDecl) {
    ParseTestFixture f;
    // Interface body 当前走 parseBlockStmt；方法签名列表尚未支持，空体可解析。
    auto unit = f.parseTopLevelDecls("interface I { }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::InterfaceDecl, 1);
}

TEST(ParserMatrixDeclInd, D06_ImplDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("struct S { x: int } impl S { func get() -> int { return 0; } }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ImplDecl, 1);
}

TEST(ParserMatrixDeclInd, D07_ImportModule) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("import Other;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ImportDecl, 1);
}

TEST(ParserMatrixDeclInd, D08_ImportFrom) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("import { foo } from Other;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ImportDecl, 1);
}

TEST(ParserMatrixDeclInd, D09_ExportList) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("func foo() -> int { return 0; } export { foo };");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ExportDecl, 1);
}

TEST(ParserMatrixDeclInd, D10_ModuleDecl) {
    ParseTestFixture f;
    auto unit = f.parseSource("module Outer { func main() -> int { return 0; } }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ModuleDecl, 1);
}

TEST(ParserMatrixDeclInd, D11_TagDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("tag MyTag;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::TagDecl, 1);
}

TEST(ParserMatrixDeclInd, D12_TagGroupDeclStub) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("taggroup G { t1, t2 }");
    f.expectNodeAbsent(unit.pool, NodeType::TagGroupDecl);
}
