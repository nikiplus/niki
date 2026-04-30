#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace niki::l1_domain {

enum class DomainSymKind : uint8_t {
    Func = 0,
    Struct,
    Kits,
    Component,
    GlobalVar,
    External
};

struct DomainSymRecord {
    uint32_t sym_id = std::numeric_limits<uint32_t>::max();
    uint32_t sym_name_sid = std::numeric_limits<uint32_t>::max();
    DomainSymKind sym_kind = DomainSymKind::External;
    uint32_t owner_mod_sid = std::numeric_limits<uint32_t>::max();
    bool is_exported = false;
};

struct KitsItemRecord {
    uint32_t alias_sid = std::numeric_limits<uint32_t>::max();
    uint32_t component_sid = std::numeric_limits<uint32_t>::max();
    bool is_mutable = false;
};

struct KitsRecord {
    uint32_t kits_sid = std::numeric_limits<uint32_t>::max();
    uint32_t owner_mod_sid = std::numeric_limits<uint32_t>::max();
    uint32_t first_item = 0;
    uint32_t item_count = 0;
};

struct ComponentRecord {
    uint32_t component_sid = std::numeric_limits<uint32_t>::max();
    uint32_t source_struct_sid = std::numeric_limits<uint32_t>::max();
    uint32_t owner_mod_sid = std::numeric_limits<uint32_t>::max();
    bool is_struct_promotion = false;
};

struct DomainIRSection {
    std::vector<DomainSymRecord> syms;
    std::vector<KitsRecord> kits;
    std::vector<KitsItemRecord> kits_items;
    std::vector<ComponentRecord> components;
};

void registerIRExtensions();

} // namespace niki::l1_domain
