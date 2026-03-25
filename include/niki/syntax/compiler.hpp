#pragma once
#include "ast.hpp"
#include "scanner.hpp"
#include "token.hpp"
#include <string_view>
#include <vector>

namespace niki::syntax {
class Compiler {
  public:
    bool compile(std::string_view source);

  private:
    Token current;
    Token previous;
    std::vector<Token> tokens; // 全量扫描的 Token 数组
    ASTPool astPool;
    ASTNodeIndex rootNode; // 必须保存整棵树的根节点索引
};
} // namespace niki::syntax
