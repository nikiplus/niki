#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/vm.hpp"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

void runRepl() {
    std::string line;
    niki::vm::VM vm;
    std::cout << "NIKI MVP v0.1\n";
    std::cout << "Type 'exit' to quit.\n";
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line) || line == "exit") {
            break;
            if (line.empty()) {
                continue;
            }
        }

        niki::syntax::Scanner scanner(line);
        std::vector<niki::syntax::Token> tokens;
        while (true) {
            auto token = scanner.scanToken();
            tokens.push_back(token);
            if (token.type == niki::syntax::TokenType::TOKEN_EOF ||
                token.type == niki::syntax::TokenType::TOKEN_ERROR) {
                break;
            }
        }
        niki::syntax::ASTPool pool;
        niki::syntax::Parser parser(line, tokens, pool);
        auto rootNode = parser.parse();

        niki::syntax::Compiler compiler;
        auto chunkResult = compiler.compile(pool, rootNode);

        if (!chunkResult.has_value()) {
            std::cerr << "Compile Error:\n";
            for (const auto &err : chunkResult.error().errors) {
                std::cerr << "  [line " << err.line << ", column " << err.column << "] " << err.message << "\n";
            }
            continue;
        }

        vm.interpret(chunkResult.value());
    }
}
int main() {
    runRepl();
    return 0;
}