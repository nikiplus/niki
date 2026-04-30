#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/linker/linker.hpp"
#include "niki/l0_core/semantic/global_compilation.hpp"
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/global_type_arena.hpp"
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
    Chunk init_chunk;
    std::unordered_map<uint32_t, uint32_t> exports;
};

/// @brief 执行单 unit 的 IR 构建、verify、lower，并产出中间工件。
std::expected<UnitCompileArtifact, diagnostic::DiagnosticBag> compileUnitChunk(GlobalCompilationUnit &unit,
                                                                                GlobalTypeArena &global_arena,
                                                                                GlobalSymbolTable &global_symbols);

/// @brief 将中间工件打包为 linker 可消费的 CompileModule。
linker::CompileModule buildCompileModule(std::string source_path, UnitCompileArtifact artifact);

/// @brief 单 unit 后端编译总入口（compileUnitChunk + buildCompileModule）。
std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedUnit(GlobalCompilationUnit &unit,
                                                                                   GlobalTypeArena &global_arena,
                                                                                   GlobalSymbolTable &global_symbols);

} // namespace niki::meta::orchestrator
