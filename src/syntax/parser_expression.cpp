#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
using namespace niki::syntax;

/*
【 为什么赋值是语句？以及 Pratt Parsing 的自动化装配线 】

在深入表达式的解析之前，我们必须先回答一个架构级的问题：
为什么 NIKI 语言中，赋值（a = 1）被设计成一个 Statement（语句），而不是 Expression（表达式）？

这源于我们对“所有权与安全性”的追求。
假设 a = b 是一次内存数据的拷贝或所有权转移。
如果允许它作为表达式，程序员就能写出极其危险的代码：
    if (a = b) { ... }  // 意外的覆盖，而非比较
    foo(a = b = c);     // 连续转移所有权，极难追踪生命周期

为了杜绝这种“幽灵副作用”，我们将赋值严格限制在“语句层”。
语句的作用是“改变环境状态，但不返回任何值”。只要在解析 '='
时，发现它嵌在表达式中间，解析器就会当场报错，把危险扼杀在摇篮里。

因此，当一段代码 `var a = 1+2*(3+4);` 经过顶层声明（parser_declaration.cpp）的预处理后，
`var a =` 已经被“建筑车间”处理完毕，并创建了 VarDeclStmt 节点。
剩下的部分 `1+2*(3+4)` 被扔进了我们现在的“表达式装配流水线”：parseExpression(None:0)。

==================== [第一道工序：起步装配 1+...] ====================
【主控装配机 parseExpression(None:0)】启动。
传送带上当前送来了 Token：
  ↓p  ↓c
+-↓-+-↓-+---+---+---+---+---+---+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

主控大喊：“前置机械臂 parsePrefix，把眼前的 1 给我抓过来！”
前置机械臂动作迅速，把 1 封装成 [LiteralExpr: 1] 的基础零件，放在了主控的左托盘（left）上。

主控抬头看传送带上的 current：哦，是个 `+` 号。
查阅《parser_precedence》：`+` 的装配优先级（进件门槛）是 Term:5。
主控心想：“我这台机器的优先级门槛是 None:0。5 > 0，说明这个 `+` 号组件我可以处理！”
启动 while 循环，传送带向前滚一格：
      ↓p  ↓c
+-+-+-↓-+-↓-+---+---+---+---+---+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

主控把左托盘的 `left` (零件 1) 递给【中继装配机 parseInfix(+)】：“把这个加法给我焊上！”

==================== [第二道工序：高优组件的拦截 2*...] ====================
【中继装配机 parseInfix(+)】接手。
它把 1 固定在底座左侧，然后它需要右侧的组件。
于是它在自己的内部，串联了一条【次级流水线 parseExpression(Term:5)】。

次级流水线启动，传送带前进：
          ↓p  ↓c
+-+-+-+-+-↓-+-↓-+---+---+---+---+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

次级流水线的主控喊前置机械臂抓取 2，拿到 [LiteralExpr: 2] 作为 left。
抬头看 current：卧槽，是个 `*` 号！
查字典：`*` 的门槛是 Factor:6。
次级流水线主控心想：“我这条线的门槛是 5，6 > 5，这个乘法组件的优先级比我高，我能接！”
启动 while 循环，传送带前进：
              ↓p  ↓c
+-+-+-+-+-+-+-↓-+-↓-+---+---+---+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

它把 2 递给新的【中继装配机 parseInfix(*)】：“去把乘法办了！”

==================== [第三道工序：隔离舱的降维打击 (3+4) ] ====================
【中继装配机 parseInfix(*)】接手，把 2 固定在左侧。
它为了拿右侧的组件，又串联了【微型流水线 parseExpression(Factor:6)】。
传送带前进：
                  ↓p  ↓c
+-+-+-+-+-+-+-+-+-↓-+-↓-+---+---+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

微型流水线一看 current 是 `(`，立刻交给前置机械臂。
**核心机制触发！** 前置机械臂看到括号，知道这不是普通组件，这是一个“优先级隔离舱”。
前置机械臂为了处理隔离舱内的蓝图，直接无视了外部的所有优先级规矩，
在内部强行开启了一条全新的、门槛最低的【独立主流水线 parseExpression(None:0)】。

==================== [第四道工序：隔离舱内部 3+4 ] ====================
【独立主流水线】抓到 3 (left)，遇到 `+` (Term:5)。
5 > 0，启动中继装配机 parseInfix(+)。
中继装配机把 3 固定，开启子流水线抓右边。
抓到 4，遇到 `)`。
字典显示：`)` 的门槛是 None:0。
子流水线的门槛是 5，0 < 5。子流水线直接停机，把 4 扔回给中继装配机。

中继装配机拿到 3 和 4，焊接成 BinaryExpr(3+4) 高级模块，交回给【独立主流水线】。
【独立主流水线】也遇到了 `)`，停机，把模块扔给前置机械臂。

前置机械臂拿到 BinaryExpr(3+4)，大喊：“隔离舱装配完毕，consume(')')，弹出舱门！”
传送带越过 `)`：
                                  ↓p  ↓c
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-↓-+-↓-+
| 1 | + | 2 | * | ( | 3 | + | 4 | ) | EOF

==================== [第五道工序：成品下线 ] ====================
前置机械臂把 BinaryExpr(3+4) 交给苦苦等待的【微型流水线】。
微型流水线遇到 EOF (0)，门槛 6 > 0，停机，交给【中继装配机 parseInfix(*)】。

【中继装配机 parseInfix(*)】将左侧的 2 和右侧的 (3+4) 焊接，
做成 BinaryExpr(2*(3+4))，交给【次级流水线】。

【次级流水线】遇到 EOF，停机，交给最初的【中继装配机 parseInfix(+)】。

【中继装配机 parseInfix(+)】将左侧的 1 和右侧的 2*(3+4) 焊接：
        [+]
       /   \
      1    [*]
          /   \
         2    [+]
             /   \
            3     4
终极成品出炉，交还给最初的【主控装配机】。
主控装配机遇到 EOF，彻底停机，将整棵 AST 树挂载到 VarDeclStmt 的 init_expr 上。
【装配完毕】
*/

// Pratt 主调度器：
// 1) 先消费一个前缀 token 产出 left
// 2) 再按“当前优先级 < 下一个运算符优先级”循环拼接中缀节点
// 3) 循环停止时，left 就是当前子表达式的完整 AST
ASTNodeIndex Parser::parseExpression(Precedence precedence) {
    advance();
    ASTNodeIndex left = parsePrefix(previous.type);
    while (precedence < getPrecedence(current.type)) {
        advance();
        left = parseInfix(previous.type, left);
    }
    return left;
};

// 前缀处理器：
// 负责字面量、标识符、类型字面量、一元运算、括号与容器字面量。
// 设计约束：只构造“当前 token 可独立决定”的节点，不处理中缀绑定。
ASTNodeIndex Parser::parsePrefix(TokenType type) {
    ASTNodePayload payload{};
    Token startToken = previous;
    switch (type) {
        //---字面量与标识符---
    case TokenType::LITERAL_INT: {
        std::string_view lexeme = source.substr(startToken.start_offset, startToken.length);
        int64_t numeric_val = std::stoll(std::string(lexeme));
        uint32_t const_idx = astPool.addConstant(vm::Value::makeInt(numeric_val));
        payload.literal.literal_type = TokenType::LITERAL_INT;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }
    case TokenType::LITERAL_FLOAT: {
        std::string_view lexeme = source.substr(startToken.start_offset, startToken.length);
        int64_t numeric_val = std::stoll(std::string(lexeme));
        uint32_t const_idx = astPool.addConstant(vm::Value::makeFloat(numeric_val));
        payload.literal.literal_type = TokenType::LITERAL_FLOAT;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }

    case TokenType::LITERAL_STRING: {
        std::string_view lexeme = source.substr(startToken.start_offset, startToken.length);
        // 去除首尾的引号
        if (lexeme.length() >= 2 && lexeme.front() == '"' && lexeme.back() == '"') {
            lexeme = lexeme.substr(1, lexeme.length() - 2);
        }
        vm::ObjString *strObj = vm::allocateString(lexeme.data(), static_cast<uint32_t>(lexeme.length()));
        payload.literal.literal_type = TokenType::LITERAL_STRING;
        payload.literal.const_pool_index = astPool.addConstant(vm::Value::makeObject(strObj));
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }

    case TokenType::LITERAL_CHAR:
        // 暂时用 Nil 占位
        payload.literal.literal_type = type;
        payload.literal.const_pool_index = astPool.addConstant(vm::Value::makeNil());
        return emitNode(NodeType::LiteralExpr, payload, startToken);

    case TokenType::KW_TRUE: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeBool(true));
        payload.literal.literal_type = TokenType::KW_TRUE;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }

    case TokenType::KW_FALSE: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeBool(false));
        payload.literal.literal_type = TokenType::KW_FALSE;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }

    case TokenType::KW_NIL: {
        uint32_t const_idx = astPool.addConstant(vm::Value::makeNil());
        payload.literal.literal_type = TokenType::KW_NIL;
        payload.literal.const_pool_index = const_idx;
        return emitNode(NodeType::LiteralExpr, payload, startToken);
    }

    //---类型标注表达式---
    case TokenType::KW_INT:
    case TokenType::KW_FLOAT:
    case TokenType::KW_STRING:
    case TokenType::KW_BOOL: {
        payload.type_expr.base_type = type;
        payload.type_expr.name_id = 0;
        return emitNode(NodeType::TypeExpr, payload, startToken);
    }

    case TokenType::IDENTIFIER: {
        std::string_view name_view = source.substr(startToken.start_offset, startToken.length);

        uint32_t interned_name_id = astPool.internString(name_view);
        payload.identifier.name_id = interned_name_id;

        return emitNode(NodeType::IdentifierExpr, payload, startToken);
    }

        //---一元前缀运算符
    case TokenType::SYM_MINUS:
    case TokenType::SYM_BANG:
    case TokenType::SYM_BIT_NOT: {
        TokenType opType = type;
        Token startToken = previous;
        ASTNodeIndex operand = parseExpression(Precedence::Unary);
        payload.unary.op = opType;
        payload.unary.operand = operand;
        return emitNode(NodeType::UnaryExpr, payload, startToken);
    }

    //---括号---
    // 这里是“表达式上下文”里的括号，不与语句级分支冲突。
    case TokenType::SYM_PAREN_L: {
        ASTNodeIndex expr = parseExpression(Precedence::None);
        consume(TokenType::SYM_PAREN_R, "Expected')'after expression.");
        return expr;
    }
    case TokenType::SYM_BRACKET_L: {
        Token startToken = previous;
        std::vector<ASTNodeIndex> elements;

        if (!check(TokenType::SYM_BRACKET_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
            do {
                if (current.type == TokenType::SYM_BRACKET_R)
                    break;
                elements.push_back(parseExpression(Precedence::None));
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_BRACKET_R, "Expected']'at end of array.");
        payload.list.elements = astPool.allocateList(elements);
        return emitNode(NodeType::ArrayExpr, payload, startToken);
    }
    // parse 阶段仅构形，不做 map key/value 类型合法性校验（交由 semantic 阶段处理）。
    case TokenType::SYM_BRACE_L: {
        Token startToken = previous;
        std::vector<ASTNodeIndex> Interleaved;
        if (!check(TokenType::SYM_BRACKET_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
            do {
                if (current.type == TokenType::SYM_BRACE_R)
                    break;
                ASTNodeIndex key = parseExpression(Precedence::None);
                Interleaved.push_back(key);
                consume(TokenType::SYM_COLON, "Expected':'after map key.");
                ASTNodeIndex value = parseExpression(Precedence::None);
                Interleaved.push_back(value);
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_BRACE_R, "Expected '}'after map elements.");
        payload.map.map_data_index = astPool.addMapData(Interleaved);
        return emitNode(NodeType::MapExpr, payload, startToken);
    }

    default:
        errorAtCurrent("Expected expression.");
        return emitNode(NodeType::ErrorNode, payload);
    }
};

// 中缀/后缀处理器：
// 入参 left 是已解析完成的左侧表达式，本函数负责补齐右侧并拼装新节点。
// 包含算术、逻辑、调用、索引、成员访问等所有需要“绑定 left”的语法形态。
ASTNodeIndex Parser::parseInfix(TokenType type, ASTNodeIndex left) {
    ASTNodePayload payload{};
    Token startToken = previous;
    switch (type) {
    case TokenType::SYM_PLUS:
    case TokenType::SYM_MINUS:
    case TokenType::SYM_CONCAT:
    case TokenType::SYM_STAR:
    case TokenType::SYM_SLASH:
    case TokenType::SYM_MOD:
    case TokenType::SYM_EQUAL_EQUAL:
    case TokenType::SYM_BANG_EQUAL:
    case TokenType::SYM_GREATER:
    case TokenType::SYM_GREATER_EQUAL:
    case TokenType::SYM_LESS:
    case TokenType::SYM_LESS_EQUAL:
    case TokenType::SYM_BIT_AND:
    case TokenType::SYM_BIT_OR:
    case TokenType::SYM_BIT_XOR:
    case TokenType::SYM_BIT_SHL:
    case TokenType::SYM_BIT_SHR:
    case TokenType::SYM_DICE: {
        payload.binary.op = type;
        payload.binary.left = left;
        Precedence precedence = getPrecedence(type);

        payload.binary.right = parseExpression(precedence);
        return emitNode(NodeType::BinaryExpr, payload, startToken);
    }
    case TokenType::SYM_AND:
    case TokenType::SYM_OR: {
        payload.logical.op = type;
        payload.logical.left = left;
        Precedence precedence = getPrecedence(type);

        payload.logical.right = parseExpression(precedence);
        return emitNode(NodeType::LogicalExpr, payload, startToken);
    }
    case TokenType::SYM_PAREN_L: {
        payload.call.callee = left;
        std::vector<ASTNodeIndex> args;
        if (!check(TokenType::SYM_PAREN_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
            do {
                if (current.type == TokenType::SYM_PAREN_R)
                    break;

                args.push_back(parseExpression(Precedence::None));
            } while (match(TokenType::SYM_COMMA));
        }
        consume(TokenType::SYM_PAREN_R, "Expected')'after arguments.");
        payload.call.arguments = astPool.allocateList(args);
        return emitNode(NodeType::CallExpr, payload, startToken);
    }
    case TokenType::SYM_BRACKET_L: {
        payload.index.target = left;
        payload.index.index = parseExpression(Precedence::None);
        consume(TokenType::SYM_BRACKET_R, "Expected ']'after index.");
        return emitNode(NodeType::IndexExpr, payload, startToken);
    }
    case TokenType::SYM_DOT: {
        payload.member.object = left;
        consume(TokenType::IDENTIFIER, "Expected property name after'.'");
        std::string_view property_name = source.substr(previous.start_offset, previous.length);
        payload.member.property_id = astPool.internString(property_name);
        return emitNode(NodeType::MemberExpr, payload, startToken);
    }

    default:
        errorAtCurrent("Unexpected infix operator.");
        return emitNode(NodeType::ErrorNode, payload, startToken);
    }
};

// 优先级查询表：
// 统一维护 token -> precedence 映射，避免在 parseInfix/parseExpression 中散落硬编码。
Precedence Parser::getPrecedence(TokenType type) const {
    // 统一维护 token -> precedence 映射，Pratt 循环只依赖这张表决策是否继续绑定。
    switch (type) {
    case TokenType::SYM_OR:
        return Precedence::Or;
    case TokenType::SYM_AND:
        return Precedence::And;

    case TokenType::SYM_BIT_OR:
        return Precedence::BitOr;
    case TokenType::SYM_BIT_XOR:
        return Precedence::BitXor;
    case TokenType::SYM_BIT_AND:
        return Precedence::BitAnd;

    case TokenType::SYM_EQUAL_EQUAL:
    case TokenType::SYM_BANG_EQUAL:
        return Precedence::Equality;

    case TokenType::SYM_GREATER:
    case TokenType::SYM_GREATER_EQUAL:
    case TokenType::SYM_LESS:
    case TokenType::SYM_LESS_EQUAL:
        return Precedence::Comparison;

    case TokenType::SYM_BIT_SHL:
    case TokenType::SYM_BIT_SHR:
        return Precedence::Shift;

    case TokenType::SYM_CONCAT:
        return Precedence::Concat;

    case TokenType::SYM_PLUS:
    case TokenType::SYM_MINUS:
        return Precedence::Term;

    case TokenType::SYM_STAR:
    case TokenType::SYM_SLASH:
    case TokenType::SYM_MOD:
        return Precedence::Factor;

    case TokenType::SYM_DICE:
        return Precedence::DICE;

    case TokenType::SYM_DOT:
    case TokenType::SYM_PAREN_L:   // 函数调用 foo()
    case TokenType::SYM_BRACKET_L: // 数组索引 arr[0]
        return Precedence::Call;

    default:
        return Precedence::None;
    }
}