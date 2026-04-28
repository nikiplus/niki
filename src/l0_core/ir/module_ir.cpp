#include "niki/l0_core/ir/module_ir.hpp"

namespace niki::ir {

//------------------------------------------------------------------------------
// INST_TABLE: 指令表基础操作。
//------------------------------------------------------------------------------
void InstTable::clear() {
    kind.clear();
    dst_kind.clear();
    dst_u32.clear();
    dst_i64.clear();
    dst_u64.clear();
    a_kind.clear();
    a_u32.clear();
    a_i64.clear();
    a_u64.clear();
    b_kind.clear();
    b_u32.clear();
    b_i64.clear();
    b_u64.clear();
    c_kind.clear();
    c_u32.clear();
    c_i64.clear();
    c_u64.clear();
    aux.clear();
    src_line.clear();
    src_col.clear();
}

bool InstTable::aligned() const {
    const size_t inst_count = kind.size();
    return dst_kind.size() == inst_count && dst_u32.size() == inst_count && dst_i64.size() == inst_count &&
           dst_u64.size() == inst_count && a_kind.size() == inst_count && a_u32.size() == inst_count &&
           a_i64.size() == inst_count && a_u64.size() == inst_count && b_kind.size() == inst_count &&
           b_u32.size() == inst_count && b_i64.size() == inst_count && b_u64.size() == inst_count &&
           c_kind.size() == inst_count && c_u32.size() == inst_count && c_i64.size() == inst_count &&
           c_u64.size() == inst_count && aux.size() == inst_count && src_line.size() == inst_count &&
           src_col.size() == inst_count;
}

// PUSH_ROW: 向 SoA 指令表追加一行指令数据。
InstId InstTable::push(InstKind inst_kind, ValueKind dst_value_kind, uint32_t dst_u32_payload, int64_t dst_i64_payload,
                       uint64_t dst_u64_payload, ValueKind first_value_kind, uint32_t first_u32_payload,
                       int64_t first_i64_payload, uint64_t first_u64_payload, ValueKind second_value_kind,
                       uint32_t second_u32_payload, int64_t second_i64_payload, uint64_t second_u64_payload,
                       ValueKind third_value_kind, uint32_t third_u32_payload, int64_t third_i64_payload,
                       uint64_t third_u64_payload, uint32_t auxiliary_data, uint32_t source_line, uint32_t source_col) {
    const InstId inst_id = static_cast<InstId>(kind.size());
    kind.push_back(inst_kind);
    dst_kind.push_back(dst_value_kind);
    dst_u32.push_back(dst_u32_payload);
    dst_i64.push_back(dst_i64_payload);
    dst_u64.push_back(dst_u64_payload);
    a_kind.push_back(first_value_kind);
    a_u32.push_back(first_u32_payload);
    a_i64.push_back(first_i64_payload);
    a_u64.push_back(first_u64_payload);
    b_kind.push_back(second_value_kind);
    b_u32.push_back(second_u32_payload);
    b_i64.push_back(second_i64_payload);
    b_u64.push_back(second_u64_payload);
    c_kind.push_back(third_value_kind);
    c_u32.push_back(third_u32_payload);
    c_i64.push_back(third_i64_payload);
    c_u64.push_back(third_u64_payload);
    aux.push_back(auxiliary_data);
    src_line.push_back(source_line);
    src_col.push_back(source_col);
    return inst_id;
}

//------------------------------------------------------------------------------
// MODULE_UTIL: ModuleIR 通用工具函数。
//------------------------------------------------------------------------------
bool ModuleIR::has_init() const { return init_func != std::numeric_limits<FuncId>::max(); }

uint32_t ModuleIR::intern(const std::string &text) {
    for (uint32_t string_id = 0; string_id < string_pool.size(); ++string_id) {
        if (string_pool[string_id] == text) {
            return string_id;
        }
    }
    string_pool.push_back(text);
    return static_cast<uint32_t>(string_pool.size() - 1);
}

} // namespace niki::ir
