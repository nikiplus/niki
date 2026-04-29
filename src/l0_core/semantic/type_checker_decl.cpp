#include "niki/l0_core/semantic/nktype.hpp"
#include "niki/l0_core/semantic/type_checker.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace niki::semantic {

/**
 * @brief 顶层声明检查分发器。
 * @param declIdx 声明节点索引。
 */
void TypeChecker::checkDeclaration(syntax::ASTNodeIndex declIdx) {
    // 声明分发器：
    // 统一把“非表达式/非语句”的节点路由到各类声明检查函数。
    const auto &node = currentPool->getNode(declIdx);
    switch (node.type) {
    case syntax::NodeType::ImportDecl:
        checkImportDecl(declIdx);
        break;
    case syntax::NodeType::FunctionDecl:
        checkFunctionDecl(declIdx);
        break;
    case syntax::NodeType::InterfaceMethod:
        checkInterfaceMethod(declIdx);
        break;
    case syntax::NodeType::StructDecl:
        checkStructDecl(declIdx);
        break;
    case syntax::NodeType::EnumDecl:
        checkEnumDecl(declIdx);
        break;
    case syntax::NodeType::TypeAliasDecl:
        checkTypeAliasDecl(declIdx);
        break;
    case syntax::NodeType::InterfaceDecl:
        checkInterfaceDecl(declIdx);
        break;
    case syntax::NodeType::ImplDecl:
        checkImplDecl(declIdx);
        break;
    case syntax::NodeType::ModuleDecl:
        checkModuleDecl(declIdx);
        break;
    case syntax::NodeType::SystemDecl:
        checkSystemDecl(declIdx);
        break;
    case syntax::NodeType::ComponentDecl:
        checkComponentDecl(declIdx);
        break;
    case syntax::NodeType::FlowDecl:
        checkFlowDecl(declIdx);
        break;
    case syntax::NodeType::KitsDecl:
        checkKitsDecl(declIdx);
        break;
    case syntax::NodeType::TagDecl:
        checkTagDecl(declIdx);
        break;
    case syntax::NodeType::TagGroupDecl:
        checkTagGroupDecl(declIdx);
        break;
    case syntax::NodeType::ProgramRoot:
        checkProgramRoot(declIdx);
        break;
    default:
        break;
    }
}

/**
 * @brief 检查模块声明主体（Two-Pass：预声明 + 细节检查）。
 * @param nodeIdx ModuleDecl 节点索引。
 */
void TypeChecker::checkModuleDecl(syntax::ASTNodeIndex nodeIdx) {
    // Two-Pass 入口：
    // Pass 1: 预声明顶层符号（函数/结构体等），解决前向引用。
    // Pass 2: 基于已注册符号做声明细节与函数体检查。
    const auto &node = currentPool->getNode(nodeIdx);
    const auto &bodyNode = currentPool->getNode(node.payload.module_decl.body);
    auto declarations = currentPool->get_list(bodyNode.payload.list.elements);

    // module 级组件名索引：供 kits 窗口目标合法性校验使用。
    moduleComponentNames.clear();
    for (auto child : declarations) {
        if (!child.isvalid()) {
            continue;
        }
        const auto &decl = currentPool->getNode(child);
        if (decl.type == syntax::NodeType::ComponentDecl) {
            moduleComponentNames.insert(decl.payload.component_decl.name_id);
        }
    }

    // 第一遍扫描：预声明所有顶层符号 (Two-Pass Compilation 第一步)
    for (auto child : declarations) {
        preDeclareNode(child);
    }

    // 第二遍扫描：检查具体的函数体和声明细节
    for (auto child : declarations) {
        checkNode(child);
    }
}

/**
 * @brief 检查 import 声明：验证所有被导入的 local name 在可见符号表中存在。
 * @param nodeIdx ImportDecl 节点索引。
 */
void TypeChecker::checkImportDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const syntax::ImportDeclData &import_data =
        currentPool->import_decl_data[node.payload.import_decl.import_decl_index];

    // module-only import：当前语义等价于“不导入具体名字”，无需检查。
    if (import_data.import_module_only) {
        return;
    }

    if (visibleSymbols == nullptr) {
        reportError(line, column, "Internal error: visible symbols not initialized.");
        return;
    }

    // 由于 import_items 存在于 side-table 中且类型为 ImportItem（非 ASTNodeIndex），这里用直接索引遍历。
    for (uint32_t offset = 0; offset < import_data.item_count; ++offset) {
        const syntax::ImportItem &item =
            currentPool->import_items[import_data.first_item_index + offset];

        if (visibleSymbols->tables.find(item.local_name_id) == visibleSymbols->tables.end()) {
            // 目前 visibleSymbols 只会包含“能用的 local name”，因此缺失即表示导出不满足。
            reportError(line, column, "Imported symbol not exported.");
        }
    }
}

/** @brief ProgramRoot 入口复用模块检查逻辑。 */
void TypeChecker::checkProgramRoot(syntax::ASTNodeIndex nodeIdx) { checkModuleDecl(nodeIdx); }
/**
 * @brief 检查函数声明（签名/参数作用域/函数体）。
 * @param nodeIdx FunctionDecl 节点索引。
 */
void TypeChecker::checkFunctionDecl(syntax::ASTNodeIndex nodeIdx) {
    // 函数检查流程：
    // 1) 提取参数/返回类型
    // 2) 建立函数局部作用域并注册参数
    // 3) 设置 currentReturnType/inFunction 后检查函数体
    // 4) 恢复外层上下文并退出作用域
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const syntax::FunctionData &func_data = currentPool->function_data[node.payload.func_decl.function_index];

    // 提取签名 (由于我们在 preDeclareFunction 已经做过一次，这里为了获取 paramTypes 我们再提取一次，
    // 也可以将结果缓存在某个地方，但对于 MVP 来说重新解析一遍开销极小)
    std::span<const syntax::ASTNodeIndex> paramNodes = currentPool->get_list(func_data.params);
    std::vector<NKType> paramTypes;
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        const syntax::ASTNode &paramNode = currentPool->getNode(paramNodes[i]);
        syntax::ASTNodeIndex type_expr_idx = paramNode.payload.var_decl.type_expr;
        paramTypes.push_back(resolveTypeAnnotation(type_expr_idx));
    }

    // 没有显式返回类型时，不强制按 void 校验，允许函数体中的 return 自由返回。
    NKType retType = NKType::makeUnknown();
    if (func_data.return_type.isvalid()) {
        retType = resolveTypeAnnotation(func_data.return_type);
    }

    // 进入函数作用域
    beginScope();

    // 注册参数到局部作用域
    for (size_t i = 0; i < paramNodes.size(); ++i) {
        auto [paramNode, p_line, p_col] = getNodeCtx(paramNodes[i]);
        uint32_t param_name_id = paramNode.payload.var_decl.name_id;
        declareSymbol(param_name_id, paramTypes[i], p_line, p_col);
    }

    NKType enclosingReturnType = currentReturnType;
    bool enclosingInFunction = inFunction;
    currentReturnType = retType;
    inFunction = true;

    if (func_data.body.isvalid()) {
        checkStatement(func_data.body);
    }

    currentReturnType = enclosingReturnType;
    inFunction = enclosingInFunction;

    endScope();
}
/** @brief 检查接口方法声明（占位实现）。 */
void TypeChecker::checkInterfaceMethod(syntax::ASTNodeIndex nodeIdx) {}
/**
 * @brief 检查结构体声明（字段类型可解析性）。
 * @param nodeIdx StructDecl 节点索引。
 */
void TypeChecker::checkStructDecl(syntax::ASTNodeIndex nodeIdx) {
    // 结构体声明检查：
    // 当前阶段仅校验字段类型标注能被解析（不存在类型会在 resolveTypeAnnotation 报错）。
    auto [node, line, column] = getNodeCtx(nodeIdx);
    uint32_t struct_idx = node.payload.struct_decl.struct_index;
    const syntax::StructData &struct_data = currentPool->struct_data[struct_idx];

    std::span<const syntax::ASTNodeIndex> types = currentPool->get_list(struct_data.types);
    for (size_t i = 0; i < types.size(); ++i) {
        resolveTypeAnnotation(types[i]); // 这里会隐式地报错如果类型不存在
    }
}
/** @brief 检查枚举声明（占位实现）。 */
void TypeChecker::checkEnumDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查类型别名声明（占位实现）。 */
void TypeChecker::checkTypeAliasDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查接口声明（占位实现）。 */
void TypeChecker::checkInterfaceDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 impl 声明（占位实现）。 */
void TypeChecker::checkImplDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 system 声明（当前先建立上下文边界，具体执行语义后续补齐）。 */
void TypeChecker::checkSystemDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    bool enclosing_system = inSystemContext;
    inSystemContext = true;

    if (node.payload.system_decl.body.isvalid()) {
        checkStatement(node.payload.system_decl.body);
    }

    inSystemContext = enclosing_system;
}
/** @brief 检查 component 声明（直接声明 + struct 提升）。 */
void TypeChecker::checkComponentDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    const auto &component_decl = node.payload.component_decl;

    if (component_decl.is_struct_promotion) {
        if (component_decl.body.isvalid()) {
            reportError(line, column, "Promoted component must not have body.");
            return;
        }
        if (component_decl.source_struct_name_id == 0) {
            reportError(line, column, "Promoted component missing source struct name.");
            return;
        }

        bool struct_found = false;
        if (globalSymbols != nullptr) {
            const auto *sym = globalSymbols->find(component_decl.source_struct_name_id);
            struct_found = (sym != nullptr && sym->kind == niki::Kind::Struct);
        }
        if (!struct_found && visibleSymbols != nullptr) {
            auto iter = visibleSymbols->tables.find(component_decl.source_struct_name_id);
            if (iter != visibleSymbols->tables.end() && iter->second.kind == niki::Kind::Struct) {
                struct_found = true;
            }
        }
        if (!struct_found) {
            reportError(line, column, "Promoted component source struct not found.");
        }
        return;
    }

    // 直接声明 component 必须具备主体块（字段语义后续细化）。
    if (!component_decl.body.isvalid()) {
        reportError(line, column, "Component declaration missing body.");
    }
}
/** @brief 检查 flow 声明（占位实现）。 */
void TypeChecker::checkFlowDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 kits 声明：建立“数据窗口 alias -> component + 权限”映射。 */
void TypeChecker::checkKitsDecl(syntax::ASTNodeIndex nodeIdx) {
    const auto [node, line, column] = getNodeCtx(nodeIdx);
    if (!node.payload.kits_decl.body.isvalid()) {
        reportError(line, column, "Invalid kits body.");
        return;
    }

    const auto &body_node = currentPool->getNode(node.payload.kits_decl.body);
    auto members = currentPool->get_list(body_node.payload.list.elements);
    // 每个 kits 名称对应一张“alias -> component + mutability”窗口表。
    auto &window = kitsWindows[node.payload.kits_decl.name_id];
    window.clear();

    for (auto member_idx : members) {
        if (!member_idx.isvalid()) {
            continue;
        }
        const auto [member_node, m_line, m_col] = getNodeCtx(member_idx);
        // Parser 已将“默认可写”映射为 VarDeclStmt，“&只读”映射为 ConstDeclStmt。
        const bool is_mutable = member_node.type == syntax::NodeType::VarDeclStmt;
        if (member_node.type != syntax::NodeType::VarDeclStmt && member_node.type != syntax::NodeType::ConstDeclStmt) {
            reportError(m_line, m_col, "Invalid kits member declaration.");
            continue;
        }

        const uint32_t alias_name_id = member_node.payload.var_decl.name_id;
        const auto type_expr_idx = member_node.payload.var_decl.type_expr;
        if (member_node.payload.var_decl.init_expr.isvalid()) {
            reportError(m_line, m_col, "Kits member must not have initializer.");
            continue;
        }
        if (!type_expr_idx.isvalid()) {
            reportError(m_line, m_col, "Kits member missing component type expression.");
            continue;
        }
        const auto &type_expr_node = currentPool->getNode(type_expr_idx);
        if (type_expr_node.type != syntax::NodeType::IdentifierExpr) {
            reportError(m_line, m_col, "Kits component name must be an identifier.");
            continue;
        }
        const uint32_t component_name_id = type_expr_node.payload.identifier.name_id;
        // kits 只能引用当前 module 内已声明的 component，避免窗口目标悬空。
        if (moduleComponentNames.find(component_name_id) == moduleComponentNames.end()) {
            reportError(m_line, m_col, "Kits target must be a component declared in current module.");
            continue;
        }

        if (window.find(alias_name_id) != window.end()) {
            reportError(m_line, m_col, "Duplicate kits alias in same kits scope.");
            continue;
        }
        // alias 在同一 kits 内必须唯一，避免 system 侧名字解析歧义。
        window.emplace(alias_name_id, KitsWindowEntry{
                                         .component_name_id = component_name_id,
                                         .is_mutable = is_mutable,
                                     });
    }
}
/** @brief 检查 tag 声明（占位实现）。 */
void TypeChecker::checkTagDecl(syntax::ASTNodeIndex nodeIdx) {}
/** @brief 检查 taggroup 声明（占位实现）。 */
void TypeChecker::checkTagGroupDecl(syntax::ASTNodeIndex nodeIdx) {}

} // namespace niki::semantic