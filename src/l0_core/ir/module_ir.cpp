#include "niki/l0_core/ir/module_ir.hpp"

/** @module_ir_impl: IR 数据平面基础操作实现
 * 这个实现文件承接 `module_ir.hpp` 的数据契约，负责最底层、最基础的三个动作：
 * 1) 指令表清空；
 * 2) SoA 列对齐校验；
 * 3) 指令行追加与字符串驻留。
 *
 * 这些操作看起来简单，但它们是整个 IR 流水线稳定性的地基。
 * builder 依赖 `push` 正确写入列，verify 依赖 `aligned` 进行结构不变量检查，
 * lower 依赖这些列在索引维度上严格一致地读取操作数。
 *
 * 其中 `aligned()` 的存在是典型的“防御式不变量检查”：
 * 在 SoA 结构里，一列偏移就会导致后续所有按列读取语义错位。
 * 因此这个文件虽然不直接做复杂算法，却承担了跨阶段数据正确性的首层保障。
 *
 * 字段流说明（核心）：
 * - `push(...)`：把一条逻辑指令拆分写入 kind/dst/a/b/c/aux/src 各列的同一索引位。
 * - `aligned()`：检查上述各列是否保持同长度，保证“同索引即同指令”成立。
 * - `intern(text)`：把文本统一落到 `string_pool`，向上游/下游提供稳定 string_id。
 */
namespace niki::ir {

//------------------------------------------------------------------------------
// INST_TABLE: 指令表基础操作。
//------------------------------------------------------------------------------
/**
 * @brief 清空指令表的全部列。
 */
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

/**
 * @brief 检查 SoA 指令表各列长度是否与 kind 列对齐。
 * @return true 全部列长度一致。
 * @return false 存在列长度不一致。
 */
bool InstTable::aligned() const {
    const size_t inst_count = kind.size();
    return
        // dst 列
        dst_kind.size() == inst_count && dst_u32.size() == inst_count && dst_i64.size() == inst_count &&
        dst_u64.size() == inst_count &&
        // a 列
        a_kind.size() == inst_count && a_u32.size() == inst_count && a_i64.size() == inst_count &&
        a_u64.size() == inst_count &&
        // b 列
        b_kind.size() == inst_count && b_u32.size() == inst_count && b_i64.size() == inst_count &&
        b_u64.size() == inst_count &&
        // c 列
        c_kind.size() == inst_count && c_u32.size() == inst_count && c_i64.size() == inst_count &&
        c_u64.size() == inst_count &&
        // 辅助列
        aux.size() == inst_count && src_line.size() == inst_count && src_col.size() == inst_count;
}

// PUSH_ROW: 向 SoA 指令表追加一行指令数据。
/**
 * @brief 向 SoA 指令表追加一条指令记录。
 * @return InstId 新增指令 id。
 */
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
/**
 * @brief 判断模块是否定义了 init 函数。
 * @return true 存在有效 init_func。
 * @return false 未设置 init_func。
 */
bool ModuleIR::has_init() const { return init_func != std::numeric_limits<FuncId>::max(); }

/**
 * @brief 将字符串驻留到模块字符串池并返回 id。
 * @param text 待驻留字符串。
 * @return uint32_t 已存在返回旧 id，不存在返回新 id。
 */
uint32_t ModuleIR::intern(const std::string &text) {
    // 线性去重保持行为可预测；后续若需性能可替换为哈希索引缓存。
    for (uint32_t string_id = 0; string_id < string_pool.size(); ++string_id) {
        if (string_pool[string_id] == text) {
            return string_id;
        }
    }
    string_pool.push_back(text);
    return static_cast<uint32_t>(string_pool.size() - 1);
}

} // namespace niki::ir
