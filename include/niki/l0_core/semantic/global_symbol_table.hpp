#pragma once

#include "niki/l0_core/semantic/nktype.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
namespace niki {
enum class Kind {
    Function,
    Struct,
    TypeAlias
};

struct GlobalSymbol {
    uint32_t name_id; ///< 符号名 id。
    Kind kind; ///< 符号种类。
    semantic::NKType type; ///< 符号类型：Function(sig_id) / Object(struct_id)。
    std::string owner_module; ///< 所属模块（source path）。
};

class GlobalSymbolTable {
  public:
    std::unordered_map<uint32_t, GlobalSymbol> symbol_table; ///< name_id -> symbol。

    /** @brief 插入全局符号；重名返回 false。 */
    bool insert(GlobalSymbol sym);
    /** @brief 按 name_id 查询全局符号。 */
    const GlobalSymbol *find(uint32_t name_id) const;
};

} // namespace niki
