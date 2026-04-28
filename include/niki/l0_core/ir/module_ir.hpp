#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
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
    uint32_t begin = 0;
    uint32_t count = 0;
};
struct FuncRecord {
    FuncId func_id = std::numeric_limits<FuncId>::max();
    uint32_t func_name_sid = std::numeric_limits<uint32_t>::max();
    uint32_t src_sid = std::numeric_limits<uint32_t>::max();
    BlockId entry_block = std::numeric_limits<BlockId>::max();
    RegId next_vreg = 0;
    Span block_span{}; // SPAN: 函数对应的块区间（位于 block 表）。
};
struct BlockRecord {
    BlockId block_id = std::numeric_limits<BlockId>::max(); // ID: 函数内相对块标识。
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
    SymId sym_id = std::numeric_limits<SymId>::max();
    uint32_t sym_name_sid = std::numeric_limits<uint32_t>::max();
    SymKind sym_kind = SymKind::External;
    uint32_t owner_mod_sid = std::numeric_limits<uint32_t>::max();
    bool is_exported = false;
};
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
    std::vector<uint32_t> aux;
    std::vector<uint32_t> src_line;
    std::vector<uint32_t> src_col;
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
struct ModuleIR {
    std::string module_name;
    std::string module_src_path;
    std::vector<std::string> string_pool;
    FuncId init_func = std::numeric_limits<FuncId>::max();
    std::vector<FuncRecord> funcs;
    std::vector<BlockRecord> blocks;
    InstTable insts;
    std::vector<SymRecord> syms;
    bool has_init() const;
    uint32_t intern(const std::string &text);
};
} // namespace niki::ir