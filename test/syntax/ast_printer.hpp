#pragma once
#include "niki/syntax/ast.hpp"
#include <sstream>
#include <string>

namespace niki::syntax::test {

class ASTPrinter {
  public:
    ASTPrinter(const ASTPool &pool) : pool(pool) {}

    std::string print(ASTNodeIndex rootIndex) {
        if (!rootIndex.isvalid())
            return "invalid";
        const ASTNode &node = pool.getNode(rootIndex);
        return printNode(node);
    }

  private:
    const ASTPool &pool;

    std::string printNode(const ASTNode &node) {
        switch (node.type) {
        case NodeType::BinaryExpr:
            return "(" + printToken(node.payload.binary.op) + " " + print(node.payload.binary.left) + " " +
                   print(node.payload.binary.right) + ")";
        case NodeType::UnaryExpr:
            return "(" + printToken(node.payload.unary.op) + " " + print(node.payload.unary.operand) + ")";
        case NodeType::LiteralExpr: {
            const auto &val = pool.constants[node.payload.literal.const_pool_index];
            if (val.type == vm::ValueType::Integer)
                return std::to_string(val.as.integer);
            if (val.type == vm::ValueType::Float)
                return std::to_string(val.as.number);
            if (val.type == vm::ValueType::Bool)
                return val.as.boolean ? "true" : "false";
            if (val.type == vm::ValueType::Object)
                return "\"<object>\""; // Object handling requires knowing the exact string struct, simplifying for now
            if (val.type == vm::ValueType::Nil)
                return "nil";
            return "literal";
        }
        case NodeType::IdentifierExpr:
            return pool.getStringId(node.payload.identifier.name_id);
        case NodeType::VarDeclStmt: {
            std::string typeStr =
                node.payload.var_decl.type_expr.isvalid() ? (" : " + print(node.payload.var_decl.type_expr)) : "";
            return "(var " + pool.getStringId(node.payload.var_decl.name_id) + typeStr + " = " +
                   print(node.payload.var_decl.init_expr) + ")";
        }
        case NodeType::ConstDeclStmt: {
            std::string typeStr =
                node.payload.var_decl.type_expr.isvalid() ? (" : " + print(node.payload.var_decl.type_expr)) : "";
            return "(const " + pool.getStringId(node.payload.var_decl.name_id) + typeStr + " = " +
                   print(node.payload.var_decl.init_expr) + ")";
        }
        case NodeType::ExpressionStmt:
            return "(expr " + print(node.payload.expr_stmt.expression) + ")";
        case NodeType::BlockStmt: {
            std::string res = "(block";
            auto elements = pool.get_list(node.payload.list.elements);
            for (auto idx : elements) {
                res += " " + print(idx);
            }
            res += ")";
            return res;
        }
        case NodeType::ProgramRoot:
        case NodeType::ModuleDecl: {
            std::string res = "(module";
            const ASTNode &bodyNode = pool.getNode(node.payload.module_decl.body);
            auto elements = pool.get_list(bodyNode.payload.list.elements);
            for (auto idx : elements) {
                res += "\n  " + print(idx);
            }
            res += ")";
            return res;
        }
        default:
            return "<unimplemented node " + std::to_string(static_cast<int>(node.type)) + ">";
        }
    }

    std::string printToken(TokenType type) {
        switch (type) {
        case TokenType::SYM_PLUS:
            return "+";
        case TokenType::SYM_MINUS:
            return "-";
        case TokenType::SYM_STAR:
            return "*";
        case TokenType::SYM_SLASH:
            return "/";
        case TokenType::SYM_MOD:
            return "%";
        case TokenType::SYM_CONCAT:
            return "..";
        case TokenType::SYM_GREATER:
            return ">";
        case TokenType::SYM_LESS:
            return "<";
        case TokenType::SYM_EQUAL_EQUAL:
            return "==";
        case TokenType::SYM_BANG_EQUAL:
            return "!=";
        case TokenType::SYM_GREATER_EQUAL:
            return ">=";
        case TokenType::SYM_LESS_EQUAL:
            return "<=";
        case TokenType::SYM_AND:
            return "&&";
        case TokenType::SYM_OR:
            return "||";
        case TokenType::SYM_BIT_AND:
            return "&";
        case TokenType::SYM_BIT_OR:
            return "|";
        case TokenType::SYM_BIT_XOR:
            return "^";
        case TokenType::SYM_BIT_SHL:
            return "<<";
        case TokenType::SYM_BIT_SHR:
            return ">>";
        case TokenType::SYM_BANG:
            return "!";
        default:
            return "?";
        }
    }
};

} // namespace niki::syntax::test
