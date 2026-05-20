#pragma once

#include "niki/l0_core/semantic/compilation_unit.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/scanner.hpp"
#include "niki/l0_core/syntax/string_interner.hpp"
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

/** @parse_test_helpers: Parser 矩阵测试夹具（不初始化 trace 日志，避免拖慢批量用例） */
class ParseTestFixture {
  public:
    niki::CompilationUnit parseSource(std::string_view source) {
        niki::CompilationUnit unit(interner_);
        unit.source = std::string(source);
        unit.source_path = "__parse_test__";
        unit.module_id = module_id_allocator_.ensure(unit.source_path);

        niki::syntax::Scanner scanner(unit.source, unit.source_path);
        while (true) {
            auto token = scanner.scanToken();
            unit.tokens.push_back(token);
            if (token.type == niki::syntax::TokenType::TOKEN_EOF) {
                break;
            }
        }
        static_cast<void>(scanner.takeDiagnostics());

        unit.pool.source_path = unit.source_path;
        niki::syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
        auto parse_result = parser.parse();
        unit.root = parse_result.root;
        return unit;
    }

    niki::CompilationUnit parseTopLevelDecls(std::string_view decls) {
        const std::string source = "module __m {" + std::string(decls) + "}";
        return parseSource(source);
    }

    niki::CompilationUnit parseStmtBody(std::string_view body, std::string_view return_type = "int") {
        const std::string source =
            "module __t{func __test_main()->" + std::string(return_type) + "{" + std::string(body) + "}}";
        return parseSource(source);
    }

    std::vector<size_t> findNodes(const niki::syntax::ASTPool &pool, niki::syntax::NodeType type) const {
        std::vector<size_t> indices;
        for (size_t i = 0; i < pool.nodes.size(); ++i) {
            if (pool.nodes[i].type == type) {
                indices.push_back(i);
            }
        }
        return indices;
    }

    static void expectNoParseErrors(const niki::CompilationUnit &unit) {
        ASSERT_TRUE(unit.root.isvalid()) << "root must be valid";
        for (size_t i = 0; i < unit.pool.nodes.size(); ++i) {
            if (unit.pool.nodes[i].type == niki::syntax::NodeType::ErrorNode) {
                FAIL() << "unexpected ErrorNode at node index " << i;
            }
        }
    }

    void expectNodeCount(const niki::syntax::ASTPool &pool, niki::syntax::NodeType type, size_t min_count) const {
        EXPECT_GE(findNodes(pool, type).size(), min_count);
    }

    void expectNodeAbsent(const niki::syntax::ASTPool &pool, niki::syntax::NodeType type) const {
        EXPECT_EQ(findNodes(pool, type).size(), 0u);
    }

  private:
    niki::syntax::StringInterner interner_;
    niki::ModuleIdAllocator module_id_allocator_;
};
