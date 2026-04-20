#pragma once
#include "niki/semantic/nktype.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace niki {
class GlobalTypeArena {
  public:
    struct StructInfo {
        uint32_t name_id;
        std::string owner_module;
        std::vector<uint32_t> field_name_ids;
        std::vector<semantic::NKType> field_types;
    };

    uint32_t internStruct(uint32_t name_id, std::string owner_module, std::vector<uint32_t> field_name_ids = {},
                          std::vector<semantic::NKType> field_types = {});
    uint32_t internFuncSig(const semantic::FunctionSignature &sig);
    const semantic::FunctionSignature *findFuncSig(uint32_t id) const;
    const StructInfo *findStruct(uint32_t id) const;

  private:
    std::vector<StructInfo> structs_;
    std::vector<semantic::FunctionSignature> func_sigs_;
};
} // namespace niki
