#include "niki/semantic/global_type_arena.hpp"

#include <utility>

namespace niki {

uint32_t GlobalTypeArena::internStruct(uint32_t name_id, std::string owner_module, std::vector<uint32_t> field_name_ids,
                                       std::vector<semantic::NKType> field_types) {
    for (uint32_t i = 0; i < structs_.size(); ++i) {
        if (structs_[i].name_id == name_id && structs_[i].owner_module == owner_module &&
            structs_[i].field_name_ids == field_name_ids && structs_[i].field_types == field_types) {
            return i;
        }
    }
    structs_.push_back(StructInfo{.name_id = name_id,
                                  .owner_module = std::move(owner_module),
                                  .field_name_ids = std::move(field_name_ids),
                                  .field_types = std::move(field_types)});
    return static_cast<uint32_t>(structs_.size() - 1);
}

uint32_t GlobalTypeArena::internFuncSig(const semantic::FunctionSignature &sig) {
    for (uint32_t i = 0; i < func_sigs_.size(); ++i) {
        if (func_sigs_[i] == sig) {
            return i;
        }
    }
    func_sigs_.push_back(sig);
    return static_cast<uint32_t>(func_sigs_.size() - 1);
}

const semantic::FunctionSignature *GlobalTypeArena::findFuncSig(uint32_t id) const {
    if (id >= func_sigs_.size()) {
        return nullptr;
    }
    return &func_sigs_[id];
}

const GlobalTypeArena::StructInfo *GlobalTypeArena::findStruct(uint32_t id) const {
    if (id >= structs_.size()) {
        return nullptr;
    }
    return &structs_[id];
}

} // namespace niki
