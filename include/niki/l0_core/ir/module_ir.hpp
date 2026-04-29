#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/** @module_ir: IR 数据平面定义
 * 这个头文件不是“算法实现”，而是 IR 层最核心的数据协议定义。它的职责是回答一个基础问题：
 * 当前端语义分析结束后，编译器应该用什么结构把程序表达成“既能被验证，又能被编码执行”的中间形态。
 *
 * 从底层执行模型看，后端 VM 本质是寄存器机：它只认识“寄存器编号、操作码、跳转目标、常量池下标”。
 * 这就要求 IR 必须显式提供：
 * 1) 虚拟寄存器（RegId）；
 * 2) 基本块与控制流边（BlockId + Jump/Branch）；
 * 3) 指令操作数槽位（dst/a/b/c）；
 * 4) 可跨阶段稳定引用的符号与字符串 id。
 *
 * 这里 `InstTable` 采用 SoA（Structure of Arrays）而不是 AoS（Array of Structs）有明确工程动机：
 * 当 verify 或 lower 需要按“列”批量扫描某一类字段（例如全部 `a_kind` 或全部 `src_line`）时，
 * SoA 在缓存局部性和遍历开销上更稳定。`aligned()` 的存在不是装饰，它是 SoA 的硬不变量守卫：
 * 任何一列长度偏移都会让后续按列读取出现语义错位甚至越界。
 *
 * `FuncRecord + BlockRecord + Span` 形成了“线性存储上的图切片”表示。函数和块并非树，而是控制流图（CFG）节点；
 * Span 让我们能在 O(1) 时间内定位某个函数在块表、某个块在指令表中的区间，不必维护复杂指针结构。
 *
 * 另外，`ValueKind + payload` 采用“标签 + 原始载荷”分离方式，目的在于把值语义与底层编码解耦：
 * builder 可以发射语义明确的立即数或引用，verify 可以检查引用域是否合法，lower 再决定最终字节编码。
 *
 * 这套定义最终把流水线稳定成：
 * IRBuilder 写入 ModuleIR -> Verify 做结构收口 -> Lower 编码为 Chunk。
 * 没有这个统一数据层，前端和后端会直接耦合，任何一端改动都会放大为全链路不稳定。
 */
namespace niki::ir {
// ID: 基础索引类型定义。
using FuncId = uint32_t;
using BlockId = uint32_t;
using InstId = uint32_t;
using RegId = uint32_t;
using SymId = uint32_t;
// VALUE: 值与指令枚举定义。
enum class ValueKind : uint8_t {
    Invalid = 0,
    VReg,
    ImmI64,
    ImmF64Bits,
    ImmBool,
    StringId,
    SymbolId,
    BlockId,
    FuncId
};
enum class InstKind : uint8_t {
    Nop = 0,
    Constant,
    Move,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    LogicAnd,
    LogicOr,
    LogicNot,
    LoadGlobal,
    StoreGlobal,
    Call,
    Return,
    NewArray,
    PushArray,
    NewMap,
    SetMap,
    GetIndex,
    SetIndex,
    GetMember,
    SetMember,
    Jump,
    Branch,
    Phi
};
struct Span {
    // 线性表区间起点（包含）。
    uint32_t begin = 0;
    // 区间长度（元素个数）。
    uint32_t count = 0;
};
//---函数记录（模块内每个函数一条）---
struct FuncRecord {
    // 函数在 funcs 表中的稳定 id。
    FuncId func_id = std::numeric_limits<FuncId>::max();
    // 函数名在模块字符串池中的 id。
    uint32_t func_name_sid = std::numeric_limits<uint32_t>::max();
    // 函数来源文件路径在字符串池中的 id（用于诊断/调试）。
    uint32_t src_sid = std::numeric_limits<uint32_t>::max();
    // 入口基本块（函数内相对 block id）。
    BlockId entry_block = std::numeric_limits<BlockId>::max();
    uint32_t param_count = 0;
    // 下一个可分配虚拟寄存器编号（函数内单调递增）。
    RegId next_vreg = 0;
    Span block_span{}; // SPAN: 函数对应的块区间（位于 block 表）。
};
//---基本块记录（按函数区间线性存储）---
struct BlockRecord {
    BlockId block_id = std::numeric_limits<BlockId>::max(); // ID: 函数内相对块标识。
    // 调试名在字符串池中的 id（如 if.then/loop.body）。
    uint32_t debug_name_sid = std::numeric_limits<uint32_t>::max();
    Span inst_span{}; // SPAN: 基本块对应的指令区间（位于 inst 表）。
};
enum class SymKind : uint8_t {
    Func = 0,
    Struct,
    GlobalVar,
    External
};
struct SymRecord {
    // 符号在 syms 表中的稳定 id。
    SymId sym_id = std::numeric_limits<SymId>::max();
    // 符号名在字符串池中的 id。
    uint32_t sym_name_sid = std::numeric_limits<uint32_t>::max();
    // 符号种类（函数/结构体/全局变量/外部符号）。
    SymKind sym_kind = SymKind::External;
    // 拥有该符号的模块名 id（跨模块诊断与链接用）。
    uint32_t owner_mod_sid = std::numeric_limits<uint32_t>::max();
    // 是否对外导出。
    bool is_exported = false;
};
//---指令表（SoA 列式布局）---
struct InstTable {
    // SOA: 指令字段采用列式存储（kind + dst/a/b/c + payload）。
    std::vector<InstKind> kind;
    std::vector<ValueKind> dst_kind;
    std::vector<uint32_t> dst_u32;
    std::vector<int64_t> dst_i64;
    std::vector<uint64_t> dst_u64;
    std::vector<ValueKind> a_kind;
    std::vector<uint32_t> a_u32;
    std::vector<int64_t> a_i64;
    std::vector<uint64_t> a_u64;
    std::vector<ValueKind> b_kind;
    std::vector<uint32_t> b_u32;
    std::vector<int64_t> b_i64;
    std::vector<uint64_t> b_u64;
    std::vector<ValueKind> c_kind;
    std::vector<uint32_t> c_u32;
    std::vector<int64_t> c_i64;
    std::vector<uint64_t> c_u64;
    std::vector<uint32_t> aux;      // 额外操作数/扩展位，按指令语义解释。
    std::vector<uint32_t> src_line; // 对应指令源码行号（调试/诊断）。
    std::vector<uint32_t> src_col;  // 对应指令源码列号（调试/诊断）。
    uint32_t size() const { return static_cast<uint32_t>(kind.size()); }
    void clear();
    bool aligned() const;
    InstId push(InstKind inst_kind, ValueKind dst_value_kind, uint32_t dst_u32_payload, int64_t dst_i64_payload,
                uint64_t dst_u64_payload, ValueKind first_value_kind, uint32_t first_u32_payload,
                int64_t first_i64_payload, uint64_t first_u64_payload, ValueKind second_value_kind,
                uint32_t second_u32_payload, int64_t second_i64_payload, uint64_t second_u64_payload,
                ValueKind third_value_kind, uint32_t third_u32_payload, int64_t third_i64_payload,
                uint64_t third_u64_payload, uint32_t auxiliary_data, uint32_t source_line, uint32_t source_col);
};
//---模块级 IR 产物（单编译单元一份）---
struct ModuleIR {
    // 模块逻辑名（通常来自源路径或模块声明）。
    std::string module_name;
    // 模块原始源路径（用于错误定位与回溯）。
    std::string module_src_path;
    // 模块级字符串池（统一存放名称、字面量等文本）。
    std::vector<std::string> string_pool;
    // 模块初始化函数 id；无初始化函数时保持 max。
    FuncId init_func = std::numeric_limits<FuncId>::max();
    // 模块内全部函数记录。
    std::vector<FuncRecord> funcs;
    // 模块内全部基本块记录（按函数区间切片）。
    std::vector<BlockRecord> blocks;
    // 模块内全部指令列。
    InstTable insts;
    // 模块符号表（导出与链接决议基础数据）。
    std::vector<SymRecord> syms;
    bool has_init() const;
    uint32_t intern(const std::string &text);
};
} // namespace niki::ir
