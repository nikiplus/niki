#pragma once
#include "niki/l0_core/semantic/nktype.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace niki {
class TypeArena {
  public:
    struct StructInfo {
        uint32_t name_id;                          ///< 结构体名 id。
        std::string owner_module;                  ///< 所属模块路径/标识。
        std::vector<uint32_t> field_name_ids;      ///< 字段名 id 列表。
        std::vector<semantic::NKType> field_types; ///< 字段类型列表。
    };

    /** @brief 注册结构体元信息并返回全局结构体 id。 */
    uint32_t internStruct(uint32_t name_id, std::string owner_module, std::vector<uint32_t> field_name_ids = {},
                          std::vector<semantic::NKType> field_types = {});
    /** @brief 注册函数签名并返回签名 id。 */
    uint32_t internFuncSig(const semantic::FunctionSignature &sig);
    /** @brief 通过 id 查询函数签名。 */
    const semantic::FunctionSignature *findFuncSig(uint32_t id) const;
    /** @brief 通过 id 查询结构体信息。 */
    const StructInfo *findStruct(uint32_t id) const;

  private:
    std::vector<StructInfo> structs_;                    ///< 结构体池。
    std::vector<semantic::FunctionSignature> func_sigs_; ///< 函数签名池。
};
} // namespace niki
