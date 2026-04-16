#pragma once

#include "niki/semantic/nktype.hpp"
#include "niki/syntax/ast_payloads.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <deque>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vcruntime_typeinfo.h>
#include <vector>

namespace niki::syntax {

/**
 * ============================================================================
 * 【编译器索引系统设计说明】
 * * @Q:为什么我们要费劲用 struct 包装 uint32_t，而不是直接用指针或 using 定义？

 * @A:
    - 如果直接用 uint32_t，代码里会充斥着 ~0U 这种魔数,用 struct 包装后，我们可以用 .isvalid()
 代替判断，更像人类的语言,也能让编译器协助我们进行检查。
    - 同时，它能防止你不小心把两个索引拿来做加减法，避免逻辑错误。

 * *@Q: 为什么不用原生指针 (ASTNode*)？
 * @A:
    - 内存搬家：我们的节点存在 vector 里，vector
 扩容时会整体“搬家”,如果是物理指针，搬家后就全部失效（野指针）了；而“索引”是相对坐标，搬到哪都一样。
    - 节省空间：64位系统指针 8 字节，uint32_t 索引只需 4 字节,用索引能让 AST 瘦身一半，缓存命中率更高。
    - 易序列化：索引是相对位置，我们可以直接把整个 vector 丢进硬盘。下次读出来，节点间的关系还在。

 * *@Q:所有变长数据(如参数列表)都在外面使用旁侧表存储，这样是为了让节点定长？为什么这样做？
 *@A:
  - 这涉及到现代CPU的优化原理，CPU在进行扫描时，在面对等长数据时可以一次性将其全部扫描，减少cache miss率，提高cpu效率。
 * ================================================================================
*/

union ASTNodePayload {
    //---表达式---
    BinaryExprPayload binary;
    LogicalExprPayload logical;
    UnaryExprPayload unary;
    LiteralExprPayload literal;
    IdentifierExprPayload identifier;
    //---复杂数据结构---
    // ArrayExprPayload 合并至 ListPayload
    TypeExprPayload type_expr;
    MapExprPayload map;
    IndexExprPayload index;
    CallExprPayload call;
    MemberExprPayload member;
    DispatchExprPayload dispatch;
    ClosureExprPayload closure;
    AwaitExprPayload await_expr;
    BorrowExprPayload borrow;
    ImplicitCastExprPayload implicit_cast;

    //---语句---
    TargetStmtPayload target_stmt;
    ExpressionStmtPayload expr_stmt;
    AssignmentStmtPayload assign_stmt;
    VarDeclStmtPayload var_decl;
    // BlockStmtPayload 合并至 ListPayload
    IfStmtPayload if_stmt;
    LoopStmtPayload loop;
    MatchStmtPayload match_stmt;
    MatchCaseStmtPayload match_case;
    ReturnStmtPayload return_stmt;
    NockStmtPayload nock;
    AttachStmtPayload attach;
    DetachStmtPayload detach;

    //---顶层声明---
    FunctionDeclPayload func_decl;
    // InterfaceMethodPayload 合并至 FunctionDeclPayload
    StructDeclPayload struct_decl;
    EnumDeclPayload enum_decl;
    TypeAliasDeclPayload type_alias;
    InterfaceDeclPayload interface_decl;
    ImplDeclPayload impl_decl;
    ModuleDeclPayload module_decl;
    SystemDeclPayload system_decl;
    ComponentDeclPayload component_decl;
    FlowDeclPayload flow_decl;
    KitsDeclPayload kits_decl;
    TagDeclPayload tag_decl;
    TagGroupDeclPayload tag_group;

    // 通用包裹列表
    ListPayload list;

    ErrorPayload error;
};

// ASTNode 是一个简单的结构体，用来存储一个 AST 节点，这个操作是为了方便我们在代码中对节点进行操作。
// 它同时存储了该节点的类型和payload。
// 我们可以用type来判断payload的类型，用payload来访问具体的数据。
// ASTNode(16byte)=type(1byte->4byte)+payload(12byte)
// 看!刚好是16byte!完全没有浪费空间!
struct ASTNode {
    NodeType type;
    ASTNodePayload payload;
};

// TokenLocation 是一个简单的结构体，用来存储一个 token 的位置信息。
// 它包含了该 token 所在的行号和列号。
struct TokenLocation {
    uint32_t line;
    uint32_t column;
};

// ASTPool 是一个简单的容器，用来存储所有的 ASTNode。
// 它的存在主要是为了方便序列化。
struct ASTPool {
    // --- 预定义的内置基础类型 ID ---
    // 它们在 ASTPool 构造时被立刻注入，保证 ID 绝对固定为 0, 1, 2, 3
    uint32_t ID_INT = 0;
    uint32_t ID_FLOAT = 1;
    uint32_t ID_BOOL = 2;
    uint32_t ID_STRING = 3;

    // 所有的ASTNode都存储在nodes中
    std::vector<ASTNode> nodes;
    // 所有的变长子节点索引都紧凑地存储在这里
    std::vector<ASTNodeIndex> lists_elements;
    //---旁侧表---
    /*为什么这里不把位置信息和node存在一起？
    我们来画图。
              node coming!↓
          +----+----+----+↓---+---
    nodes:|nod0|nod1|nod2|type|...
          +----+----+----+↓---+---
    sametime:             ↓
          +----+----+----+↓---+---
    locat:|loc0|loc1|loc2|HERE|...
          +----+----+----+----+---
    看到吗？由于location信息和type信息是同时被记录的，因此它们在数组上的位置也一定是同步的，这就好像我们用一根尺子推过并排的两个凹槽，这两个凹槽被填满的进度一定是相同的。
    也正因为如此，我们进行数据清理的时候要记住同时清理两个数组否则就会索引错位，也因如此，我们在清理的时候事实上通常不会单个单个清理，而是一次性销毁两个数组内的所有内容。
    这样只要保证数组是只进不出的，其数据就一定是同步的——除非有傻子传完第一个不传第其它的，这样的人是要被打屁股的
    我们的旁侧表等信息也都是用这个手段进行存储的。
    */
    std::vector<TokenLocation> locations; // 报错时使用，用于报错定位。
    // 存储每个节点的推导类型（由 TypeChecker 填充，供 Compiler 使用）
    std::vector<semantic::NKType> node_types;

    // 前端常量池（Side-buffer）：
    // 为了保证 ASTNode 严格维持 16 字节大小（4字节对齐），我们绝对不能把包含 8 字节对齐（double/void*）的 vm::Value
    // 直接塞进 ASTNodePayload。 这会导致整个 ASTNode 被迫 8 字节对齐，并产生 4 字节 padding，体积膨胀 50%（从 16 变成
    // 24 字节）。 因此，解析期遇到的所有字面量，都会被打包成 Value 存放在这里，AST 中仅保留 4 字节的 const_pool_index。
    std::vector<vm::Value> constants;

    std::vector<FunctionData> function_data; // 函数数据
    std::vector<StructData> struct_data;     // 结构体数据
    std::vector<ImplData> impl_data;         // Impl实现数据
    std::vector<KitsData> kits_data;
    std::vector<MapData> map_data; // Map字典数据

    // 获取或注册函数签名，返回其在池中的索引
    std::vector<semantic::FunctionSignature> func_sigs;
    uint32_t internFuncSignature(const std::vector<semantic::NKType> &params, semantic::NKType retType) {
        semantic::FunctionSignature sig{params, retType};
        for (size_t i = 0; i < func_sigs.size(); ++i) {
            if (func_sigs[i] == sig) {
                return i;
            }
        }
        func_sigs.push_back(sig);
        return func_sigs.size() - 1;
    }

    // 使用 std::deque 替代 std::vector 来存储字符串。
    // std::deque 在向两端添加元素时，不会使现有的引用或指针失效。
    // 这样，string_to_id 里面存储的 std::string_view 就能永远安全地指向底层字符串内存！
    std::deque<std::string> string_pool;
    std::unordered_map<std::string_view, uint32_t> string_to_id;

    // 我们使用std::span提供了一个get_list方法，为的是方便我们获取一个列表的所有元素索引，这在我们需要遍历一个列表时非常方便。
    // std::span相当于一个指向数组的视图，只要我们提供这个数组的起始地址和长度，它就会返回一个指向这个数组的子数组的视图。
    // 注意！std::span不负责管理内存，是只读的，我们不能用它来修改数组的内容。这样同时导致其是安全和高性能的。
    std::span<const ASTNodeIndex> get_list(ASTListIndex list_info) const;

    ASTPool(); // 构造函数声明，用于初始化内置类型字符串

    ASTNodeIndex allocateNode(NodeType type);
    // 调用ASTListIndex，装载指定区域的astnode，并返回对应的astlist切片。
    ASTListIndex allocateList(std::span<const ASTNodeIndex> elements);
    //---辅助函数---
    uint32_t addConstant(vm::Value value);
    void clear();
    ASTNode &getNode(ASTNodeIndex index);
    const ASTNode &getNode(ASTNodeIndex index) const;
    uint32_t internString(std::string_view str);
    const std::string &getStringId(uint32_t id) const;

    // --- Map Data Allocation ---
    uint32_t addMapData(const std::vector<ASTNodeIndex> &interleaved) {
        std::vector<ASTNodeIndex> keys;
        std::vector<ASTNodeIndex> values;
        keys.reserve(interleaved.size() / 2);
        values.reserve(interleaved.size() / 2);
        for (size_t i = 0; i < interleaved.size(); i += 2) {
            keys.push_back(interleaved[i]);
            values.push_back(interleaved[i + 1]);
        }

        MapData data;
        data.keys = allocateList(keys);
        data.values = allocateList(values);

        map_data.push_back(data);
        return static_cast<uint32_t>(map_data.size() - 1);
    }
};

} // namespace niki::syntax