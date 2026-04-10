#include "niki/debug/logger.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/vm.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <sstream>
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

        if (parser.hasError()) {
            std::cerr << "Syntax Error: Parsing failed.\n";
            continue;
        }

        niki::semantic::TypeChecker checker;
        auto checkResult = checker.check(pool, rootNode);
        if (!checkResult.has_value()) {
            std::cerr << "Type Error:\n";
            for (const auto &err : checkResult.error().errors) {
                std::cerr << "  [line " << err.line << ", column " << err.column << "] " << err.message << "\n";
            }
            continue;
        }

        niki::syntax::Compiler compiler;
        auto chunkResult = compiler.compile(pool, rootNode, checkResult.value().type_table);

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
void runFile(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open file." << path << "\n";
        exit(74); // IO错误（文件打不开）
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    niki::vm::VM vm;
    niki::syntax::Scanner scanner(source);
    std::vector<niki::syntax::Token> tokens;
    while (true) {
        auto token = scanner.scanToken();
        tokens.push_back(token);
        if (token.type == niki::syntax::TokenType::TOKEN_EOF || token.type == niki::syntax::TokenType::TOKEN_ERROR) {
            break;
        }
    }
    niki::syntax::ASTPool pool;
    niki::syntax::Parser parser(source, tokens, pool);
    auto rootNode = parser.parse();

    if (parser.hasError()) {
        std::cerr << "Syntax Error: Parsing failed.\n";
        exit(65); // EX_DATAERR
    }

    niki::semantic::TypeChecker checker;
    auto checkResult = checker.check(pool, rootNode);
    if (!checkResult.has_value()) {
        std::cerr << "Type Error:\n";
        for (const auto &err : checkResult.error().errors) {
            std::cerr << "  [line " << err.line << ", column " << err.column << "] " << err.message << "\n";
        }
        exit(65);
    }

    niki::syntax::Compiler compiler;
    auto chunkResult = compiler.compile(pool, rootNode, checkResult.value().type_table);

    if (!chunkResult.has_value()) {
        std::cerr << "Compile Error:\n";
        for (const auto &err : chunkResult.error().errors) {
            std::cerr << " [line" << err.line << ", column " << err.column << "] " << err.message << "\n";
        }
        exit(65); // 数据格式错误（对应我们的 编译期语法错误 ）
    }

    auto result = vm.interpret(chunkResult.value());
    if (result == niki::vm::InterpretResult::RUNTIME_ERROR) {
        exit(70); // 内部软件错误（对应我们的 VM运行时错误 ）
    }
}
/*- argc == 1 ：没有参数，启动 REPL 模式。
- argc == 2 ：传入了文件路径，读取整个文件内容，走一次完整的编译执行流（ runFile ）。
- argc > 2 ：抛出 Usage 错误并退出。
*/
int main(int argc, char *argv[]) {
    niki::debug::initLogger(spdlog::level::info, "logs/niki.log");
    spdlog::info("NIKI started argc={}", argc);
    if (argc == 1) {
        runRepl();
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        std::cerr << "Usage :niki[path]\n";
        exit(64); // 用法错误（命令行参数不对）
    }

    return 0;
}