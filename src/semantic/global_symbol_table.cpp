#include "niki/semantic/global_symbol_table.hpp"

#include <utility>

namespace niki {

bool GlobalSymbolTable::insert(GlobalSymbol sym) {
    const uint32_t name_id = sym.name_id;
    auto [it, inserted] = symbol_table.emplace(name_id, std::move(sym));
    return inserted;
}

const GlobalSymbol *GlobalSymbolTable::find(uint32_t name_id) const {
    auto it = symbol_table.find(name_id);
    if (it == symbol_table.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace niki
