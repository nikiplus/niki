#pragma once
#include "niki/l0_core/ir/module_ir.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/object.hpp"
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** @lower_to_chunk: IR 到字节码的编码桥接层
 * Lower 层的职责非常具体：把结构化 IR 变成 VM 能直接执行的字节码对象。
 * IR 里我们谈的是 InstKind、BlockId、RegId、SymbolId；VM 里只剩 opcode 字节、操作数字节和常量池索引。
 * 这不是简单拷贝，而是一次“语义编码”过程。
 *
 * 这个模块存在的必要性来自执行机理本身：
 * 处理器/虚拟机不会执行抽象语义节点，只会按 PC 读取字节流并解码。
 * 因此必须有一个阶段把“语义级别的操作”映射为“可取指、可解码、可执行”的离散编码。
 *
 * 关键难点在控制流。IR 中跳转目标通常是逻辑块 id，而字节码跳转需要的是相对偏移。
 * 所以 lower 采用经典两阶段算法：
 * 1) 第一遍发射时记录 jump 占位与 patch 信息；
 * 2) 第二遍根据目标块入口 code index 回填偏移，并区分前跳与回环跳。
 * 没有这个 patch 机制，循环和分支无法稳定落地。
 *
 * 另一个核心点是指令映射契约。`InstKind -> OPCODE` 不应散落在多处，而应集中维护，
 * 这样可以显式表达“哪些语义已落地、哪些仍不可执行”，并与 VM 能力表同步。
 * 常量加载同理，需要按常量池下标宽度选择窄/宽指令，否则编码与解码会不一致。
 *
 * 最终输出是 `ObjFunction` 列表（Chunk + 常量池 + 调试位置信息），供 runtime/VM 直接消费。
 * 这层把“可理解的 IR”转换成“可运行的程序表示”，是编译链路真正落地执行语义的最后一步。
 */
namespace niki::ir {
//---模块级降解结果---
// 包含按 ModuleIR.funcs 顺序生成的函数对象列表。
struct LowerResult {
    // 降级后的函数对象集合（由 runtime/VM 消费）。
    std::vector<vm::ObjFunction *> functions;
};

//---指令映射契约项---
// 描述一个 InstKind 在 lowering 阶段是否有可执行 opcode 映射。
struct InstOpcodeChecklistEntry {
    // IR 指令种类。
    InstKind inst_kind;
    // 对应 VM opcode；未支持时为空。
    std::optional<vm::OPCODE> opcode;
    // 对该映射项的人类可读说明（用于审计/调试）。
    std::string_view note;
};

//---映射清单查询---
// 返回 InstKind -> opcode 对照表（含未支持项）。
const std::vector<InstOpcodeChecklistEntry> &instOpcodeChecklist();

//---模块级降解入口---
// 输入：ModuleIR
// 输出：LowerResult（函数列表）或错误信息
// NOTE: 当前实现按 ModuleIR.funcs 顺序生成 ObjFunction 列表。
std::expected<LowerResult, std::string> lowerModuleToChunk(const ModuleIR &module_ir);
//---单函数降解入口---
// 输入：ModuleIR + FuncId
// 输出：单个 ObjFunction 或错误信息（便于调试定位）
std::expected<vm::ObjFunction *, std::string> lowerFunctionToChunk(const ModuleIR &module_ir, FuncId function_id);
} // namespace niki::ir
