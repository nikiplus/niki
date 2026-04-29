#include "niki/l0_core/semantic/global_symbol_table.hpp"

#include <utility>

namespace niki {

/**
 * @brief 插入全局符号。
 * @param sym 待插入符号。
 * @return 插入成功返回 true；重名返回 false。
 */
bool GlobalSymbolTable::insert(GlobalSymbol sym) {
    const uint32_t name_id = sym.name_id;
    auto [it, inserted] = symbol_table.emplace(name_id, std::move(sym));
    return inserted;
}

/**
 * @brief 查询全局符号。
 * @param name_id 符号名 id。
 * @return 命中返回符号指针，否则 nullptr。
 */
const GlobalSymbol *GlobalSymbolTable::find(uint32_t name_id) const {
    auto it = symbol_table.find(name_id);
    if (it == symbol_table.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace niki
