#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

/** @linker_facade: 链接阶段对外契约与入口
 * `CompileModule` / `LinkedProgram` / `LinkOptions` 为跨层稳定类型；`Linker` 为门面见 `ProjectLinker` 的实现说明。
 */
namespace niki::linker {

//---编译产物(每个.nk一个)---
struct CompileModule {
    // 模块逻辑名（当前由文件名 stem 推导）。
    std::string module_name;
    // 原始源文件路径（用于报错定位）。
    std::string source_path;
    // 该模块编译后的初始化 chunk（模块加载时执行）。
    Chunk init_chunk;
    // 导出表：symbol_id -> exported_symbol_id。
    // MVP 阶段两者相同，后续可扩展可见性与重命名导出。
    std::unordered_map<uint32_t, uint32_t> exports;
    // 非函数导出符号记录（component/kits 等），供 Linker 入表。
    std::vector<ir::SymRecord> exported_symbols;
};

//---链接产物（整个项目一个）---
struct LinkedProgram {
    // 链接后保留的初始化块集合（运行期按顺序执行）。
    std::vector<Chunk> init_chunks;
    // 决议后的入口函数 name_id；无入口时保持 UINT32_MAX。
    uint32_t entry_name_id = UINT32_MAX;
    // 项目级合并后的字符串池（用于诊断、调试与后续重映射）。
    std::vector<std::string> string_pool;
};

//---链接配置---
struct LinkOptions {
    std::string entry_name = "main";
};

class Linker {
  public:
    std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> link(const std::vector<CompileModule> &modules,
                                                                       const LinkOptions &options);
};

} // namespace niki::linker
