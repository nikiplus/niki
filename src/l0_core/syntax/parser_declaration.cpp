#include "niki/l0_core/syntax/ast.hpp"
#include "niki/l0_core/syntax/parser.hpp"
#include "niki/l0_core/syntax/parser_precedence.hpp"
#include "niki/l0_core/syntax/token.hpp"
#include <cstdint>
#include <string_view>
#include <vector>
using namespace niki::syntax;

/**
 * @brief 解析顶层声明入口，仅接受声明关键字。
 * @return 声明节点；不合法输入返回 ErrorNode。
 */
ASTNodeIndex Parser::parseTopLevelDeclaration() {
    // DISPATCH_RULE: 顶层语法严格采用“关键字驱动分发”，避免把语句误解析到模块根级别。
    // 这样可保证后续语义阶段默认“模块根只含声明”这一不变量。
    if (match(TokenType::KW_IMPORT)) {
        return parseImportDecl();
    } else if (match(TokenType::KW_EXPORT)) {
        return parseExportDecl();
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

    errorAtCurrent("Top-level only supports declarations.");
    advance();
    return emitNode(NodeType::ErrorNode, ASTNodePayload{});
}

/**
 * @brief 解析块内声明入口（声明优先，失败回退语句）。
 * @return 声明或语句节点。
 */
ASTNodeIndex Parser::parseDeclaration() {
    if (match(TokenType::KW_IMPORT) || match(TokenType::KW_EXPORT)) {
        errorAtCurrent("import/export declarations are only allowed at top level.");
        return emitNode(NodeType::ErrorNode, ASTNodePayload{});
    } else if (match(TokenType::KW_VAR)) {
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
/**
 * @brief 解析函数声明。
 * @return FunctionDecl 节点。
 */
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

            // 参数本质上就是带有可选类型标注的变量声明，但它没有 '=' 和 ';'
            // 为了复用 VarDeclStmtPayload 的结构且避免报错，这里直接 emit
            params.push_back(emitNode(NodeType::VarDeclStmt, param_payload, previous));
        } while (match(TokenType::SYM_COMMA));
    }
    consume(TokenType::SYM_PAREN_R, "Expected')' after fuction name.");
    func_data.params = astPool.allocateList(params);

    if (match(TokenType::SYM_ARROW)) {
        func_data.return_type = parseExpression(Precedence::None);
    } else {
        func_data.return_type = ASTNodeIndex::invalid();
    }

    consume(TokenType::SYM_BRACE_L, "Expected '{' before function body.");
    func_data.body = parseBlockStmt();

    astPool.function_data.push_back(func_data);
    payload.func_decl.function_index = astPool.function_data.size() - 1;
    return emitNode(NodeType::FunctionDecl, payload, startToken);
}
/**
 * @brief 解析结构体声明并写入 StructData 旁侧表。
 * @return StructDecl 节点。
 */
ASTNodeIndex Parser::parseStructDecl() {
    // 结构体声明：解析 `name : type` 字段并将 names/types 分离写入 StructData。
    Token startToken = previous;
    ASTNodePayload payload{};

    StructData struct_data{};

    // 1. 解析结构体名
    consume(TokenType::IDENTIFIER, "Expected struct name.");
    struct_data.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    // 2. 解析结构体字段 (直接在 parser 里将 name 和 type 拆分存储到 side-table)
    consume(TokenType::SYM_BRACE_L, "Expected '{' before struct body.");

    std::vector<ASTNodeIndex> field_names;
    std::vector<ASTNodeIndex> field_types;

    while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
        // 解析字段名
        consume(TokenType::IDENTIFIER, "Expected field name.");
        ASTNodePayload name_payload{};
        name_payload.identifier.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
        field_names.push_back(emitNode(NodeType::IdentifierExpr, name_payload, previous));

        // 解析冒号
        consume(TokenType::SYM_COLON, "Expected ':' after field name.");

        // 解析类型
        field_types.push_back(parseExpression(Precedence::None));

        // 可选的逗号或分号
        if (check(TokenType::SYM_COMMA) || check(TokenType::SYM_SEMICOLON)) {
            advance();
        }
    }
    consume(TokenType::SYM_BRACE_R, "Expected '}' after struct body.");

    struct_data.names = astPool.allocateList(field_names);
    struct_data.types = astPool.allocateList(field_types);

    astPool.struct_data.push_back(struct_data);
    payload.struct_decl.struct_index = static_cast<uint32_t>(astPool.struct_data.size() - 1);

    return emitNode(NodeType::StructDecl, payload, startToken);
}
/** @brief 解析枚举声明（占位实现）。 */
ASTNodeIndex Parser::parseEnumDecl() { return ASTNodeIndex{}; }
/**
 * @brief 解析类型别名声明。
 * @return TypeAliasDecl 节点。
 */
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

/**
 * @brief 解析接口声明。
 * @return InterfaceDecl 节点。
 */
ASTNodeIndex Parser::parseInterfaceDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected alias name.");
    payload.interface_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before interface body.");
    payload.interface_decl.body = parseBlockStmt();
    return emitNode(NodeType::InterfaceDecl, payload, startToken);
}

/**
 * @brief 解析 impl 声明（target [for trait] + methods）。
 * @return ImplDecl 节点。
 */
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

/**
 * @brief 解析 import 声明（模块导入或命名导入）。
 * @return ImportDecl 节点。
 */
ASTNodeIndex Parser::parseImportDecl() {
    Token startToken = previous; // already consumed 'import'
    ASTNodePayload payload{};
    ImportDeclData data{};

    if (check(TokenType::IDENTIFIER)) {
        advance();
        data.module_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
        data.first_item_index = static_cast<uint32_t>(astPool.import_items.size());
        data.item_count = 0;
        data.import_module_only = true;
        consume(TokenType::SYM_SEMICOLON, "Expected ';' after import declaration.");
    } else {
        consume(TokenType::SYM_BRACE_L, "Expected '{' after 'import'.");
        data.first_item_index = static_cast<uint32_t>(astPool.import_items.size());
        while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
            consume(TokenType::IDENTIFIER, "Expected imported symbol name.");
            uint32_t imported_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
            uint32_t local_name_id = imported_name_id;
            if (match(TokenType::KW_AS)) {
                consume(TokenType::IDENTIFIER, "Expected alias name after 'as'.");
                local_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
            }
            astPool.import_items.push_back(ImportItem{
                .imported_name_id = imported_name_id,
                .local_name_id = local_name_id,
            });

            if (!match(TokenType::SYM_COMMA)) {
                break;
            }
        }
        consume(TokenType::SYM_BRACE_R, "Expected '}' after import item list.");
        consume(TokenType::KW_FROM, "Expected 'from' in named import declaration.");
        consume(TokenType::IDENTIFIER, "Expected module name after 'from'.");
        data.module_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
        data.item_count = static_cast<uint32_t>(astPool.import_items.size()) - data.first_item_index;
        data.import_module_only = false;
        consume(TokenType::SYM_SEMICOLON, "Expected ';' after import declaration.");
    }

    astPool.import_decl_data.push_back(data);
    payload.import_decl.import_decl_index = static_cast<uint32_t>(astPool.import_decl_data.size() - 1);
    return emitNode(NodeType::ImportDecl, payload, startToken);
}

/**
 * @brief 解析 export 声明（导出列表或包裹声明）。
 * @return ExportDecl 节点。
 */
ASTNodeIndex Parser::parseExportDecl() {
    Token startToken = previous; // already consumed 'export'
    ASTNodePayload payload{};
    ExportDeclData data{};
    data.first_item_index = static_cast<uint32_t>(astPool.export_items.size());
    data.item_count = 0;
    data.wrapped_decl = ASTNodeIndex::invalid();
    data.has_wrapped_decl = false;

    if (match(TokenType::SYM_BRACE_L)) {
        while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
            consume(TokenType::IDENTIFIER, "Expected exported symbol name.");
            uint32_t local_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
            uint32_t exported_name_id = local_name_id;
            if (match(TokenType::KW_AS)) {
                consume(TokenType::IDENTIFIER, "Expected exported alias name after 'as'.");
                exported_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
            }
            astPool.export_items.push_back(ExportItem{
                .local_name_id = local_name_id,
                .exported_name_id = exported_name_id,
            });
            if (!match(TokenType::SYM_COMMA)) {
                break;
            }
        }
        consume(TokenType::SYM_BRACE_R, "Expected '}' after export item list.");
        consume(TokenType::SYM_SEMICOLON, "Expected ';' after export declaration.");
        data.item_count = static_cast<uint32_t>(astPool.export_items.size()) - data.first_item_index;
    } else {
        ASTNodeIndex wrapped_decl = ASTNodeIndex::invalid();
        if (match(TokenType::KW_FUNC)) {
            wrapped_decl = parseFunctionDecl();
        } else if (match(TokenType::KW_STRUCT)) {
            wrapped_decl = parseStructDecl();
        } else if (match(TokenType::KW_ENUM)) {
            wrapped_decl = parseEnumDecl();
        } else if (match(TokenType::KW_TYPE)) {
            wrapped_decl = parseTypeAliasDecl();
        } else if (match(TokenType::KW_INTERFACE)) {
            wrapped_decl = parseInterfaceDecl();
        } else if (match(TokenType::KW_IMPL)) {
            wrapped_decl = parseImplDecl();
        } else if (match(TokenType::KW_MODULE)) {
            wrapped_decl = parseModuleDecl();
        } else if (match(TokenType::KW_SYSTEM)) {
            wrapped_decl = parseSystemDecl();
        } else if (match(TokenType::KW_COMPONENT)) {
            wrapped_decl = parseComponentDecl();
        } else if (match(TokenType::KW_FLOW)) {
            wrapped_decl = parseFlowDecl();
        } else if (match(TokenType::KW_KITS)) {
            wrapped_decl = parseKitsDecl();
        } else if (match(TokenType::KW_TAG)) {
            wrapped_decl = parseTagDecl();
        } else if (match(TokenType::KW_TAGGROUP)) {
            wrapped_decl = parseTagGroupDecl();
        } else {
            errorAtCurrent("Expected declaration after 'export'.");
            wrapped_decl = emitNode(NodeType::ErrorNode, ASTNodePayload{});
        }

        data.wrapped_decl = wrapped_decl;
        data.has_wrapped_decl = true;
    }

    astPool.export_decl_data.push_back(data);
    payload.export_decl.export_decl_index = static_cast<uint32_t>(astPool.export_decl_data.size() - 1);
    return emitNode(NodeType::ExportDecl, payload, startToken);
}
//---NIKI特有---
/**
 * @brief 解析 module 声明。
 * @return ModuleDecl 节点。
 */
ASTNodeIndex Parser::parseModuleDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected module name.");
    payload.module_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before module body.");

    // module 主体块需要允许 module-scoped import/export。
    // parseBlockStmt 会走 parseDeclaration，而 parseDeclaration 默认会拒绝 import/export。
    // 因此这里单独实现 module body 的声明扫描逻辑。
    Token bodyStartToken = previous; // '{'
    std::vector<ASTNodeIndex> declarations;
    while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
        if (match(TokenType::KW_IMPORT)) {
            declarations.push_back(parseImportDecl());
            continue;
        }
        if (match(TokenType::KW_EXPORT)) {
            declarations.push_back(parseExportDecl());
            continue;
        }
        declarations.push_back(parseDeclaration());
    }
    consume(TokenType::SYM_BRACE_R, "Expected '}' after module body.");

    ASTNodePayload blockPayload{};
    blockPayload.list.elements = astPool.allocateList(declarations);
    payload.module_decl.body = emitNode(NodeType::BlockStmt, blockPayload, bodyStartToken);

    return emitNode(NodeType::ModuleDecl, payload, startToken);
}

/**
 * @brief 解析 system 声明（依赖表达式 + 主体块）。
 * @return SystemDecl 节点。
 */
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

/**
 * @brief 解析 component 声明。
 * @return ComponentDecl 节点。
 */
ASTNodeIndex Parser::parseComponentDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    payload.component_decl.is_struct_promotion = false;
    payload.component_decl.source_struct_name_id = 0;

    // 支持两种声明形式：
    // 1) 直接声明：component Name { ... }
    // 2) struct 提升：component StructName as ComponentName;
    consume(TokenType::IDENTIFIER, "Expected component name or source struct name.");
    const uint32_t first_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

    if (match(TokenType::KW_AS)) {
        // component <StructName> as <ComponentName>;
        payload.component_decl.is_struct_promotion = true;
        payload.component_decl.source_struct_name_id = first_name_id;
        consume(TokenType::IDENTIFIER, "Expected component alias name after 'as'.");
        payload.component_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
        consume(TokenType::SYM_SEMICOLON, "Expected ';' after component promotion declaration.");
        payload.component_decl.body = ASTNodeIndex::invalid();
    } else {
        // component <ComponentName> { ... }
        payload.component_decl.name_id = first_name_id;
        consume(TokenType::SYM_BRACE_L, "Expected '{' before component body.");
        payload.component_decl.body = parseBlockStmt();
    }

    return emitNode(NodeType::ComponentDecl, payload, startToken);
}

/**
 * @brief 解析 flow 声明。
 * @return FlowDecl 节点。
 */
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

/**
 * @brief 解析 kits 声明（组件窗口定义）。
 * @return KitsDecl 节点。
 */
ASTNodeIndex Parser::parseKitsDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected kits name.");
    payload.kits_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_BRACE_L, "Expected '{' before kits body.");

    // kits 主体最小语法（MVP）：
    //   <Component> as <alias>;      // 默认可写
    //   &<Component> as <alias>;     // 只读
    // 语义约定：默认 -> VarDeclStmt；'&' 前缀 -> ConstDeclStmt。
    // 这里复用 Var/ConstDeclStmt 是为了复用后续 TypeChecker 的“可写/只读”判定分支。
    Token bodyStartToken = previous; // '{'
    std::vector<ASTNodeIndex> members;
    while (!check(TokenType::SYM_BRACE_R) && !isAtEnd(TokenType::TOKEN_EOF)) {
        const bool is_readonly = match(TokenType::SYM_BIT_AND);
        const bool is_mutable = !is_readonly;

        consume(TokenType::IDENTIFIER, "Expected component name (optionally prefixed by '&') in kits body.");
        ASTNodePayload type_payload{};
        type_payload.identifier.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
        ASTNodeIndex component_type_expr = emitNode(NodeType::IdentifierExpr, type_payload, previous);

        consume(TokenType::KW_AS, "Expected 'as' after component name.");
        consume(TokenType::IDENTIFIER, "Expected alias name after 'as'.");
        const uint32_t alias_name_id = astPool.internString(source.substr(previous.start_offset, previous.length));

        consume(TokenType::SYM_SEMICOLON, "Expected ';' after kits item.");

        ASTNodePayload member_payload{};
        member_payload.var_decl.name_id = alias_name_id;
        member_payload.var_decl.type_expr = component_type_expr;
        member_payload.var_decl.init_expr = ASTNodeIndex::invalid();
        // kits 条目本质是“类型映射声明”，不允许在语法层携带初始化表达式。
        members.push_back(
            emitNode(is_mutable ? NodeType::VarDeclStmt : NodeType::ConstDeclStmt, member_payload, previous));
    }
    consume(TokenType::SYM_BRACE_R, "Expected '}' after kits body.");

    ASTNodePayload block_payload{};
    block_payload.list.elements = astPool.allocateList(members);
    payload.kits_decl.body = emitNode(NodeType::BlockStmt, block_payload, bodyStartToken);
    return emitNode(NodeType::KitsDecl, payload, startToken);
}

/**
 * @brief 解析 tag 声明。
 * @return TagDecl 节点。
 */
ASTNodeIndex Parser::parseTagDecl() {
    Token startToken = previous;
    ASTNodePayload payload{};
    consume(TokenType::IDENTIFIER, "Expected tag name.");
    payload.tag_decl.name_id = astPool.internString(source.substr(previous.start_offset, previous.length));
    consume(TokenType::SYM_SEMICOLON, "Expected ';' after tag declaration.");
    return emitNode(NodeType::TagDecl, payload, startToken);
}
/** @brief 解析 taggroup 声明（占位实现）。 */
ASTNodeIndex Parser::parseTagGroupDecl() { return ASTNodeIndex{}; }
