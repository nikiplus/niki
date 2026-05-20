#include "../helpers/parse_test_helpers.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax;

/** @parser_decl_dependent: 含体声明 Parser 矩阵（ID D-20～D-25） */

TEST(ParserMatrixDeclDep, D20_ComponentWithBody) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("component Player { }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ComponentDecl, 1);
}

TEST(ParserMatrixDeclDep, D21_ComponentStructPromotion) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("struct vec { x: int } component vec as vec_com;");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ComponentDecl, 1);
}

TEST(ParserMatrixDeclDep, D22_KitsDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls(
        "component C { } kits W { C as alias; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::KitsDecl, 1);
}

TEST(ParserMatrixDeclDep, D23_SystemDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls(
        "component C { } kits W { C as c; } system Sys (W) { return 0; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::SystemDecl, 1);
}

TEST(ParserMatrixDeclDep, D24_FlowDecl) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("flow MainFlow { nock; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::FlowDecl, 1);
}

TEST(ParserMatrixDeclDep, D25_ExportWrappedFunc) {
    ParseTestFixture f;
    auto unit = f.parseTopLevelDecls("export func main() -> int { return 0; }");
    ParseTestFixture::expectNoParseErrors(unit);
    f.expectNodeCount(unit.pool, NodeType::ExportDecl, 1);
    f.expectNodeCount(unit.pool, NodeType::FunctionDecl, 1);
}
