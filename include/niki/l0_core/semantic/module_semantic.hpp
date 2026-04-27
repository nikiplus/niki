
#include "niki/l0_core/semantic/global_symbol_table.hpp"
#include "niki/l0_core/semantic/nktype.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
namespace niki::semantic {

struct SymbolRef {
    uint32_t owner_module_id;
    uint32_t name_id;
    niki::Kind kind;
    NKType type;
};

struct ImportBinding {
    uint32_t from_module_id;
    uint32_t imported_name_id;
    uint32_t local_name_id; // as后的本地名
};

struct ExportBinding {
    uint32_t local_name_id;  // 本模块内部名
    uint32_t export_name_id; // 对外名(可通过as改名)
};

struct UnitVisibleSymbols {
    // 本单元最终可见名->符号引用
    std::unordered_map<uint32_t, SymbolRef> tables;
};

struct ModuleMeta {
    uint32_t module_id;
    std::string source_path;
    size_t unit_index;

    std::vector<ImportBinding> imports;
    std::vector<ExportBinding> exports;
};

struct ModuleRegistry {
    std::vector<ModuleMeta> modules;
    std::unordered_map<uint32_t, size_t> module_id_to_meta_index;
};

struct ModuleExportTable {
    // module_id->(exported_name_id->symbol)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, SymbolRef>> table;
};

} // namespace niki::semantic