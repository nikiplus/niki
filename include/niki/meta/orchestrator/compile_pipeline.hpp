#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/l0_core/semantic/compilation_unit.hpp"
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/semantic/module_namespace.hpp"
#include "niki/l0_core/semantic/module_semantic.hpp"
#include "niki/l0_core/semantic/type_arena.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include <expected>
#include <string>
#include <unordered_map>

/** @meta_compile_pipeline_api: 编排层下沉的后端编译阶段 API
 * 该头文件只暴露“单编译单元 -> CompileModule”的后端阶段能力，
 * 用于把 orchestrator 的流程控制与实际编译细节解耦。
 */
namespace niki::meta::orchestrator {

struct UnitCompileArtifact {
    // 模块稳定 id（由 compileUnitChunk 从 unit.module_id 回填）。
    ModuleId module_id = kInvalidModuleId;
    // 模块逻辑名（由 IRBuilder 从 ModuleDecl 提取；为空时由 buildCompileModule 回退到文件名 stem）。
    std::string module_name;
    Chunk init_chunk;
    std::unordered_map<uint32_t, uint32_t> exports;
    // 非函数导出符号记录（component/kits 等），供 Linker 入表。
    std::vector<ir::SymRecord> exported_sym_records;
};

/// @brief 执行单 unit 的 IR 构建、verify、lower，并产出中间工件。
std::expected<UnitCompileArtifact, diagnostic::DiagnosticBag> compileUnitChunk(
    CompilationUnit &unit, TypeArena &global_arena, const semantic::UnitVisibleSymbols *visible_symbols = nullptr);

/// @brief 将中间工件打包为 linker 可消费的 CompileModule。
linker::CompileModule buildCompileModule(std::string source_path, ModuleId module_id, UnitCompileArtifact artifact);

/// @brief 单 unit 后端编译（假定调用方已完成 predeclare 与 TypeChecker）。编排器批量语义后使用。
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedBackend(
    CompilationUnit &unit, TypeArena &global_arena, const semantic::UnitVisibleSymbols *visible_symbols = nullptr);

/// @brief 单 unit 完整编译：predeclare → 模块可见性 → typecheck → IR/verify/lower → CompileModule。
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedUnit(CompilationUnit &unit,
                                                                                  TypeArena &global_arena,
                                                                                  ModuleNamespace &module_namespace);

} // namespace niki::meta::orchestrator
