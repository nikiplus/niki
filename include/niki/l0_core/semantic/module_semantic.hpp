#pragma once
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
namespace niki::semantic {

struct SymbolRef {
    uint32_t owner_module_id; ///< 定义该符号的模块 id。
    uint32_t name_id; ///< 符号名 id。
    niki::Kind kind; ///< 符号种类（函数/结构体等）。
    NKType type; ///< 符号语义类型。
};

struct ImportBinding {
    uint32_t from_module_id; ///< 来源模块 id。
    uint32_t imported_name_id; ///< 来源模块导出名 id。
    uint32_t local_name_id; ///< 本地可见名 id（as 后名称）。
};

struct ExportBinding {
    uint32_t local_name_id; ///< 本模块内部名 id。
    uint32_t export_name_id; ///< 对外导出名 id（可通过 as 改名）。
};

struct UnitVisibleSymbols {
    std::unordered_map<uint32_t, SymbolRef> tables; ///< 本单元最终可见名 -> 符号引用。
};

struct ModuleMeta {
    uint32_t module_id; ///< 模块 id。
    std::string source_path; ///< 源路径。
    size_t unit_index; ///< 所属编译单元索引。

    std::vector<ImportBinding> imports; ///< 导入绑定集合。
    std::vector<ExportBinding> exports; ///< 导出绑定集合。
};

struct ModuleRegistry {
    std::vector<ModuleMeta> modules; ///< 模块元信息表。
    std::unordered_map<uint32_t, size_t> module_id_to_meta_index; ///< module_id -> modules 下标。
};

struct ModuleExportTable {
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, SymbolRef>> table; ///< module_id -> (export_name -> symbol)。
};

} // namespace niki::semantic