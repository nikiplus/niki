#pragma once

#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

/** @module_namespace: 模块作用域符号注册与查询
 *
 * 核心改进：符号 key = (module_id, name_id)，天然支持跨模块同名符号。
 * 同时维护反向索引 module_id → [Key]，提供 O(1) 模块符号查询。
 *
 * 与 GlobalSymbolTable 的对比：
 * - key: (module_id, name_id) vs name_id
 * - 跨模块同名: 允许 vs 冲突
 * - per-module 查询: O(1) vs O(N)
 * - owner_module: ModuleId (uint32_t) vs string
 *
 * ModuleNamespace 是唯一的模块级符号注册与查询入口。
 */
namespace niki {

class ModuleNamespace {
  public:
    struct Symbol {
        uint32_t name_id;
        ModuleId owner_module_id;
        Kind kind;             // Function / Struct / TypeAlias
        semantic::NKType type; // Function(sig_id) / Object(struct_id)
    };

    /// @brief 插入符号。同模块内重名返回 false；跨模块重名允许。
    bool insert(Symbol sym);

    /// @brief 按完整 key 查询。
    const Symbol *find(ModuleId module_id, uint32_t name_id) const;

    /// @brief 按模块 id 获取该模块所有符号（用于构建 visible symbols 和导出表）。
    std::vector<const Symbol *> findModuleSymbols(ModuleId module_id) const;

    /// @brief 当前存储的符号总数。
    size_t size() const { return symbols_.size(); }

  private:
    struct Key {
        ModuleId module_id;
        uint32_t name_id;
        bool operator==(const Key &o) const { return module_id == o.module_id && name_id == o.name_id; }
    };
    struct KeyHash {
        size_t operator()(Key k) const {
            return std::hash<uint64_t>{}((static_cast<uint64_t>(k.module_id) << 32) | k.name_id);
        }
    };

    std::unordered_map<Key, Symbol, KeyHash> symbols_;

    // 反向索引: module_id → [Key] (O(1) 快速获取模块全部符号)
    // 存储 Key 而非指针，避免 insert 触发 rehash 导致指针失效。
    std::unordered_map<ModuleId, std::vector<Key>> module_symbols_;
};

} // namespace niki
