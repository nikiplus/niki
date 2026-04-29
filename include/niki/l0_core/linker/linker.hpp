#pragma once
#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

/** @linker: 模块产物到项目程序的组装层
 * 这个头文件定义链接阶段的数据契约。编译阶段产出的是“每个模块各自可运行”的中间结果，
 * 但项目执行需要的是“单一入口 + 一组已决议符号 + 可装载程序体”。
 * Linker 的职责就是把分散模块转换成统一的 `LinkedProgram`。
 *
 * 从系统层面看，链接阶段解决的是“全局一致性”问题，而不是“局部语法/类型”问题：
 * - 是否存在重复导出符号；
 * - 入口函数是否存在且唯一；
 * - 模块初始化块如何按项目语义组织。
 * 这些问题只有在“拿到全部模块”后才能回答，因此必须独立成项目级阶段。
 *
 * `CompileModule` 与 `LinkedProgram` 的拆分也有现实必要性：
 * - CompileModule 面向编译器后端，描述“单模块产物”；
 * - LinkedProgram 面向 runtime/launcher，描述“可直接启动”的整体程序。
 * 这层抽象让编译与运行解耦，便于在中间插入检查、统计或重定位策略演进。
 *
 * 头文件中保留了 merge/remap/resolve 等私有步骤接口，表示链接流程并非单函数黑盒，
 * 而是由多个可演进子步骤构成：字符池合并、操作数重映射、符号决议、初始化块拼装。
 * 即使当前实现是 MVP，这些边界先行定义可以稳定未来重构成本。
 */
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