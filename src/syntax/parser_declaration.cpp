#include "niki/syntax/ast.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/parser_precedence.hpp"
#include "niki/syntax/token.hpp"
#include <cstdint>
#include <string_view>
#include <vector>
using namespace niki::syntax;

// 专门用于解析“绝对顶层”的声明（如全局文件、module内部）
// 严格禁止普通语句（Statement）或表达式（Expression）在此出现！
ASTNodeIndex Parser::parseTopLevelDeclaration() {
    if (match(TokenType::KW_FUNC)) {
        return parseFunctionDecl();
    } else if (match(TokenType::KW_STRUCT)) {
        return parseStructDecl();
    } else if (match(TokenType::KW_ENUM)) {
        return parseEnumDecl();
    } else if (match(TokenType::KW_TYPE)) {
        return parseTypeAliasDecl();
    } else if (match(TokenType::KW_INTERFACE)) {
        return parseInterfaceDecl();
    } else if (match(TokenType::KW_IMPL)) {
        return parseImplDecl();
    } else if (match(TokenType::KW_MODULE)) {
        return parseModuleDecl();
    } else if (match(TokenType::KW_SYSTEM)) {
        return parseSystemDecl();
    } else if (match(TokenType::KW_COMPONENT)) {
        return parseComponentDecl();
    } else if (match(TokenType::KW_FLOW)) {
        return parseFlowDecl();
    } else if (match(TokenType::KW_KITS)) {
        return parseKitsDecl();
    } else if (match(TokenType::KW_TAG)) {
        return parseTagDecl();
    } else if (match(TokenType::KW_TAGGROUP)) {
        return parseTagGroupDecl();
    }

    // 到了这里，说明遇到了非法结构（比如 '1+2;'，'return;' 等）
    // 我们必须当场报错，绝不能降级给 parseStatement！
    errorAtCurrent(
        "Expressions and statements are strictly forbidden at the top level. Only declarations are allowed.");

    // 为了防止死循环（卡在错误字符上），我们需要强行吃掉错误字符，或者进行同步（synchronize）
    advance();
    synchronize();
    return emitNode(NodeType::ErrorNode, ASTNodePayload{});
}

// 普通代码块（BlockStmt）内部的声明解析入口
// 允许回退到普通语句（Statement）
ASTNodeIndex Parser::parseDeclaration() {
    if (match(TokenType::KW_VAR)) {
        return parseVarDeclStmt();
    } else if (match(TokenType::KW_CONST)) {
        return parseConstDeclStmt();
    } else if (match(TokenType::KW_FUNC)) {
        return parseFunctionDecl();
    } else if (match(TokenType::KW_STRUCT)) {
        return parseStructDecl();
    } else if (match(TokenType::KW_ENUM)) {
        return parseEnumDecl();
    } else if (match(TokenType::KW_TYPE)) {
        return parseTypeAliasDecl();
    } else if (match(TokenType::KW_INTERFACE)) {
        return parseInterfaceDecl();
    } else if (match(TokenType::KW_IMPL)) {
        return parseImplDecl();
    } else if (match(TokenType::KW_MODULE)) {
        return parseModuleDecl();
    } else if (match(TokenType::KW_SYSTEM)) {
        return parseSystemDecl();
    } else if (match(TokenType::KW_COMPONENT)) {
        return parseComponentDecl();
    } else if (match(TokenType::KW_FLOW)) {
        return parseFlowDecl();
    } else if (match(TokenType::KW_KITS)) {
        return parseKitsDecl();
    } else if (match(TokenType::KW_TAG)) {
        return parseTagDecl();
    } else if (match(TokenType::KW_TAGGROUP)) {
        return parseTagGroupDecl();
    }

    // 如果以上都不是，则它肯定是一个语句（或表达式语句）
    return parseStatement();
}

//---基础声明---
ASTNodeIndex Parser::parseFunctionDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};

    FunctionData func_data{};

    consume(TokenType::IDENTIFIER, "Expected fuction name.");
    func_data.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    consume(TokenType::SYM_PAREN_L, "Expected'(' after fuction name.");
    std::vector<ASTNodeIndex> params;
    if (!check(TokenType::SYM_PAREN_R)) {
        do {
            consume(TokenType::IDENTIFIER, "Expected parameter name.");
            ASTNodePayload param_payload{};
            param_payload.var_decl.name_id =
                astPool.internString(source.substr(previous.start_offset, previous.length));

            if (match(TokenType::SYM_COLON)) {
                param_payload.var_decl.type_expr = parseExpression(Precedence::None);
            } else {
                param_payload.var_decl.type_expr = ASTNodeIndex::invalid();
            }
            param_payload.var_decl.init_expr = ASTNodeIndex::invalid();

            params.push_back(emitNode(NodeType::VarDeclStmt, param_payload, previous));
        } while (match(TokenType::SYM_COMMA));
    }
    consume(TokenType::SYM_PAREN_R, "Expected')' after fuction name.");
    func_data.params = astPool.allocateList(params);

    func_data.return_type = ASTNodeIndex::invalid();

    consume(TokenType::SYM_BRACE_L, "Expected '{' before function body.");
    func_data.body = parseBlockStmt();

    astPool.function_data.push_back(func_data);
    payload.func_decl.function_index = astPool.function_data.size() - 1;
    return emitNode(NodeType::FunctionDecl, payload, startToken);
}
ASTNodeIndex Parser::parseStructDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};

    // 1. 解析组件名
    consume(TokenType::IDENTIFIER, "Expected component name.");
    payload.component_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    // 2. 解析组件体 (复用 parseBlockStmt，将语义验证留给下游 Checker)
    consume(TokenType::SYM_BRACE_L, "Expected '{' before component body.");
    payload.component_decl.body = parseBlockStmt();

    return emitNode(NodeType::StructDecl, payload, startToken);
}
ASTNodeIndex Parser::parseEnumDecl() { return ASTNodeIndex{}; }
ASTNodeIndex Parser::parseTypeAliasDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected alias name.");
    payload.type_alias.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_EQUAL, "Expected '=' after type alias name.");
    payload.type_alias.type_expr = parseExpression(Precedence::None);
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after type alias declaration.");

    return emitNode(NodeType::TypeAliasDecl, payload, startToken);
}

ASTNodeIndex Parser::parseInterfaceDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected alias name.");
    payload.interface_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before interface body.");
    payload.interface_decl.body = parseBlockStmt();
    return emitNode(NodeType::InterfaceDecl, payload, startToken);
}

ASTNodeIndex Parser::parseImplDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    ImplData data{};
    data.target_type = parseExpression(Precedence::None);

    if (match(TokenType::KW_FOR)) {
        data.trait_type = parseExpression(Precedence::None);
    } else {
        data.trait_type = ASTNodeIndex::invalid();
    }

    consume(TokenType::SYM_BRACE_L, "Expected '{' before impl body.");
    std::vector<ASTNodeIndex> methods;
    while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
        methods.push_back(parseFunctionDecl());
    }
    consume(TokenType::SYM_BRACE_R, "Expected '}' after impl body");
    data.methods = astPool.allocateList(methods);

    uint32_t data_index = static_cast<uint32_t>(astPool.impl_data.size());
    astPool.impl_data.push_back(data);
    payload.impl_decl.impl_index = data_index;
    return emitNode(NodeType::ImplDecl, payload, startToken);
}
//---NIKI特有---
ASTNodeIndex Parser::parseModuleDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected module name.");
    payload.module_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before module body.");
    payload.module_decl.body = parseBlockStmt();
    return emitNode(NodeType::ModuleDecl, payload, startToken);
}
ASTNodeIndex Parser::parseSystemDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};

    consume(TokenType::IDENTIFIER, "Expected system name.");
    payload.component_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    consume(TokenType::SYM_PAREN_L, "Expected '(' after system name to declare dependencies.");
    payload.system_decl.system_data = parseExpression(Precedence::None);
    consume(TokenType::SYM_PAREN_R, "Expected ')' after system dependencies.");

    consume(TokenType::SYM_BRACE_L, "Expected '{' before system body.");
    payload.system_decl.body = parseBlockStmt();

    return emitNode(NodeType::SystemDecl, payload, startToken);
}
ASTNodeIndex Parser::parseComponentDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};

    // 1. 解析组件名
    consume(TokenType::IDENTIFIER, "Expected component name.");
    payload.component_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    // 2. 解析组件体 (复用 parseBlockStmt，将语义验证留给下游 Checker)
    consume(TokenType::SYM_BRACE_L, "Expected '{' before component body.");
    payload.component_decl.body = parseBlockStmt();

    return emitNode(NodeType::ComponentDecl, payload, startToken);
}

ASTNodeIndex Parser::parseFlowDecl() {
    Token startToken = previous; // 已消费 'flow'
    ASTNodePayload payload{};

    // 1. 解析流程名
    consume(TokenType::IDENTIFIER, "Expected flow name.");
    payload.flow_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    // 2. 解析流程体 (必须是一个代码块，里面允许出现 nock 和 await)
    consume(TokenType::SYM_BRACE_L, "Expected '{' before flow body.");
    payload.flow_decl.body = parseBlockStmt();

    return emitNode(NodeType::FlowDecl, payload, startToken);
}

// kits具体实现未敲定，暂且先保留为类似于struct和component的数据格式。
ASTNodeIndex Parser::parseKitsDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected kits name.");
    payload.kits_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before kits body.");
    payload.kits_decl.body = parseBlockStmt();
    return emitNode(NodeType::KitsDecl, payload, startToken);
}

ASTNodeIndex Parser::parseTagDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected tag name.");
    payload.tag_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after tag declaration.");
    return emitNode(NodeType::TagDecl, payload, startToken);
}
ASTNodeIndex Parser::parseTagGroupDecl() { return ASTNodeIndex{}; }
