/*
 * =============================================================================
 * AST 与 ASTPool 设计说明（原 ast.hpp 长注释迁移至此，与实现同文件便于维护）
 * =============================================================================
 *
 * --- ASTNodeIndex：为何用 struct 包一层 uint32_t，而不用裸索引或 ASTNode*？---
 * - .isvalid() 可读性优于魔数 ~0U；不同类型索引不易被误混算。
 * - 节点放在 std::vector 中，扩容会整体搬迁；物理指针会全部失效，索引是相对下标，始终有效。
 * - 64 位下指针 8 字节，uint32_t 索引 4 字节，AST 更紧凑、缓存更友好。
 * - 序列化：整池 vector 写出再读回，边关系仍成立。
 *
 * --- 旁侧表：变长数据为何不塞进 ASTNode？---
 * - 让 ASTNode 保持定长（当前 16 字节），列表与大块数据用「扁平区 + 下标切片」外置。
 * - 扫描等长节点数组对 CPU 缓存更友好；变长内容集中在 lists_elements 与各 decl 旁侧 vector。
 *
 * --- locations / node_types 与 nodes 的对齐关系（旁侧表核心约束）---
 * allocateNode 时同时对 nodes、locations、node_types 追加一条记录，三数组按下标 i 一一对应：
 *   nodes[i]  <->  locations[i]  <->  node_types[i]
 * 清理时必须同步（见 clear()）；只增不减时天然保持同步。
 * （图示：两列凹槽被同一把尺子推进，填满进度一致——原理同上。）
 *
 * --- constants 与 vm::Value---
 * vm::Value 可能含 8 字节对齐成员；若塞进 ASTNodePayload 会撑大整个 ASTNode 并引入 padding。
 * 字面量在解析期写入 constants，AST 里只存 const_pool_index。
 *
 * --- 字符串驻留表放在哪里？---
 * 字符串池上移到 Driver 级 GlobalInterner，ASTPool 仅做转发。
 * 这样多模块编译可共享同一套 name_id，避免链接阶段出现“同ID异名”的伪冲突。
 *
 * --- get_list 与 std::span---
 * get_list 返回对 lists_elements 某一段的只读视图，不持有内存所有权；遍历列表时零拷贝切片。
 * =============================================================================
 */

#include "niki/l0_core/syntax/ast.hpp"

#include "niki/l0_core/semantic/nktype.hpp"

#include <cstdio>
#include <cstdlib>

using namespace niki::syntax;

ASTPool::ASTPool(GlobalInterner &shared_interner) : interner(&shared_interner) {
    ID_INT = interner->intern("int");
    ID_FLOAT = interner->intern("float");
    ID_BOOL = interner->intern("bool");
    ID_STRING = interner->intern("string");
}

std::span<const ASTNodeIndex> ASTPool::get_list(ASTListIndex list_info) const {
    if (!list_info.isvalid() || list_info.length == 0) {
        return {};
    }
    return {lists_elements.data() + list_info.start_index, list_info.length};
}
ASTNodeIndex ASTPool::allocateNode(NodeType type) {
    uint32_t index = static_cast<uint32_t>(nodes.size());
    nodes.push_back(ASTNode{type, {}});
    locations.push_back(TokenLocation{0, 0});
    node_types.push_back(semantic::NKType::makeUnknown()); // 默认初始化为未知类型
    return ASTNodeIndex{index};
};
// 调用ASTListIndex，装载指定区域的astnode，并返回对应的astlist切片。
ASTListIndex ASTPool::allocateList(std::span<const ASTNodeIndex> elements) {
    uint32_t start_index = static_cast<uint32_t>(lists_elements.size());
    // 这里之所以不使用for循环，而是使用std::vector::insert,是因为insert方法会预先计算elements的大小，使vector只需进行一次内存再分配即可容纳所有元素。
    // 而如果使用push_back，每当存储的elements量超出vector当前申请的内存大小，便会使std::vector在底层使用memove进行内存的批量拷贝，这是极消耗性能的
    lists_elements.insert(lists_elements.end(), elements.begin(), elements.end());
    return ASTListIndex{start_index, static_cast<uint32_t>(elements.size())};
};
//---辅助函数---
uint32_t ASTPool::addConstant(vm::Value value) {
    uint32_t index = static_cast<uint32_t>(constants.size());
    constants.push_back(value);
    return index;
};
void ASTPool::clear() {
    source_path.clear();
    nodes.clear();
    locations.clear();
    node_types.clear();
    lists_elements.clear();
    constants.clear();
    function_data.clear();
    struct_data.clear();
    kits_data.clear();

    // 注意：共享字符串池由 GlobalInterner 持有，clear() 仅重置 AST 结构数据。
};
ASTNode &ASTPool::getNode(ASTNodeIndex index) {
    if (!index.isvalid() || index >= nodes.size()) {
        std::fprintf(stderr, "ASTNodeIndex is invalid or out of bounds.\n");
        std::abort();
    }
    return nodes[index.index];
}
const ASTNode &ASTPool::getNode(ASTNodeIndex index) const {
    if (!index.isvalid() || index >= nodes.size()) {
        std::fprintf(stderr, "ASTNodeIndex is invalid or out of bounds.\n");
        std::abort();
    }
    return nodes[index.index];
}

uint32_t ASTPool::internString(std::string_view str) {
    if (interner == nullptr) {
        std::fprintf(stderr, "ASTPool interner is not initialized.\n");
        std::abort();
    }
    return interner->intern(str);
};

const std::string &ASTPool::getStringId(uint32_t id) const {
    if (interner == nullptr) {
        std::fprintf(stderr, "ASTPool interner is not initialized.\n");
        std::abort();
    }
    return interner->get(id);
}

std::vector<std::string> ASTPool::snapshotStringPool() const {
    if (interner == nullptr) {
        std::fprintf(stderr, "ASTPool interner is not initialized.\n");
        std::abort();
    }
    return interner->snapshot();
}
