#include "niki/l0_core/semantic/type_arena.hpp"

#include <utility>

namespace niki {

/**
 * @brief 注册结构体信息到类型池。
 * @return 结构体 id（若完全相同则返回已存在 id）。
 */
uint32_t TypeArena::internStruct(uint32_t name_id, std::string owner_module, std::vector<uint32_t> field_name_ids,
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

/** @brief 注册函数签名到类型池。 */
uint32_t TypeArena::internFuncSig(const semantic::FunctionSignature &sig) {
    for (uint32_t i = 0; i < func_sigs_.size(); ++i) {
        if (func_sigs_[i] == sig) {
            return i;
        }
    }
    func_sigs_.push_back(sig);
    return static_cast<uint32_t>(func_sigs_.size() - 1);
}

/** @brief 通过 id 查询函数签名。 */
const semantic::FunctionSignature *TypeArena::findFuncSig(uint32_t id) const {
    if (id >= func_sigs_.size()) {
        return nullptr;
    }
    return &func_sigs_[id];
}

/** @brief 通过 id 查询结构体信息。 */
const TypeArena::StructInfo *TypeArena::findStruct(uint32_t id) const {
    if (id >= structs_.size()) {
        return nullptr;
    }
    return &structs_[id];
}

} // namespace niki
