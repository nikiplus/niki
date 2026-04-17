#pragma once
#include "niki/vm/chunk.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace niki::linker {

//---统一错误码---
enum class LinkErrorCode {
    DUPLICATE_SYMBOL,
    ENTRY_NOT_FOUND,
    MULTIPLE_ENTRY,
    STRING_REMAP_FAILED,
    CONSTANT_REMAP_FAILED
};

struct LinkError {
    LinkErrorCode code;
    std::string message;
    std::string module_path;
};

//---编译产物(每个.nk一个)---
// 这里是compile后交给linker 的单位
struct CompileModule {
    std::string module_name;
    std::string source_path;
    Chunk init_chunk;
    std::unordered_map<uint32_t, uint32_t> exports;
};

//---链接产物（整个项目一个）---
struct LinkedProgram {
    std::vector<Chunk> init_chunks;
    uint32_t entry_name_id = UINT32_MAX;
    std::vector<std::string> string_pool;
};

//---链接配置---
struct LinkOptions {
    std::string entry_name = "main";
    bool allow_no_entry = false; // repl/脚本模式可放宽
};

class Linker {
  public:
    std::expected<LinkedProgram, std::vector<LinkError>> link(const std::vector<CompileModule> &modules,
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