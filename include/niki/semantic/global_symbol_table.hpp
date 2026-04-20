#pragma once

#include "niki/semantic/nktype.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
namespace niki {
enum class Kind {
    Function,
    Struct
};

struct GlobalSymbol {
    uint32_t name_id;
    Kind kind;
    semantic::NKType type;    // Function(global_sig_id)/Obj(global_struct_id)
    std::string owner_module; // source path
};

class GlobalSymbolTable {
  public:
    std::unordered_map<uint32_t, GlobalSymbol> symbol_table;

    bool insert(GlobalSymbol sym); // false = duplicate
    const GlobalSymbol *find(uint32_t name_id) const;
};

} // namespace niki
