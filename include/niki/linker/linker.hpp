#pragma once
#include "niki/diagnostic/diagnostic.hpp"
#include "niki/vm/chunk.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace niki::linker {

//---编译产物(每个.nk一个)---
// 这里是compile后交给linker 的单位
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
    bool allow_no_entry = false; // repl/脚本模式可放宽
};

class Linker {
  public:
    // 链接总入口：完成符号冲突检测、入口决议，并构造 LinkedProgram。
    // 当前实现是 MVP：先保证“可联编可运行”，复杂重定位接口保留在 private。
    std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> link(const std::vector<CompileModule> &modules,
                                                                        const LinkOptions &options);

  private:
    // 1)合并字符池，返回old_id -> new_id remap
    bool mergeStringPools(/*in/out params*/);
    // 2)对每个模块chunk执行name_id/常量索引重映射
    bool remapChunkOperands(/*in/out params*/);
    // 3)符号冲突检查+入口决议
    bool resolveSymbols(/*in/out params*/);
    // 4）生成merged_init_chunk
    Chunk mergeInitChunks(/*in params*/);
};

} // namespace niki::linker