#include "niki/syntax/ast.hpp"

#include "niki/semantic/nktype.hpp"

using namespace niki::syntax;

ASTPool::ASTPool() {
    // 在池子诞生之初，强制注入内置基础类型，将它们的 ID 永久固化！
    ID_INT = internString("int");
    ID_FLOAT = internString("float");
    ID_BOOL = internString("bool");
    ID_STRING = internString("string");
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
    nodes.clear();
    locations.clear();
    node_types.clear();
    lists_elements.clear();
    constants.clear();
    function_data.clear();
    struct_data.clear();
    kits_data.clear();

    // 注意：不能 clear string_pool，否则我们硬编码的内置类型 ID 就失效了！
    // 真正的重置应该是让 string_pool 退回到初始化状态，但为了简单，目前我们不清空字符串池。
};
ASTNode &ASTPool::getNode(ASTNodeIndex index) {
    if (!index.isvalid() || index >= nodes.size()) {
        throw std::out_of_range("ASTNodeIndex is invalid or out of bounds.");
    }
    return nodes[index.index];
}
const ASTNode &ASTPool::getNode(ASTNodeIndex index) const {
    if (!index.isvalid() || index >= nodes.size()) {
        throw std::out_of_range("ASTNodeIndex is invalid or out of bounds.");
    }
    return nodes[index.index];
}

uint32_t ASTPool::internString(std::string_view str) {
    auto it = string_to_id.find(str);
    if (it != string_to_id.end()) {
        return it->second;
    }
    uint32_t new_id = static_cast<uint32_t>(string_pool.size());
    string_pool.emplace_back(str);
    string_to_id[string_pool.back()] = new_id;
    return new_id;
};

const std::string &ASTPool::getStringId(uint32_t id) const { return string_pool.at(id); }
