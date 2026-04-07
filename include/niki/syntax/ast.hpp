#pragma once

#include "niki/semantic/nktype.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vcruntime_typeinfo.h>
#include <vector>

namespace niki::syntax {
// 我们对opcode和token进行抽象，使“符”（token）在这次抽象中成为"意图"(opcode)
// 这里比较难理解，稍微详细的解释可以到虚拟机指北中查看
// 我们可以将ast节点分为如下三个大类
enum class NodeType : uint8_t {
    /*---表达式---*/
    // 计算并产生一个值，也就是说，表达式在执行完毕后，必定会在虚拟机的操作数栈上，留下一个（或多个）确切值的代码片段
    //---基础计算---
    BinaryExpr,     // 二元表达式
    UnaryExpr,      // 一元表达式
    LiteralExpr,    // 字面量表达式
    IdentifierExpr, // 标识符表达式
    //---复杂数据结构---
    ArrayExpr, // 数组表达式
    MapExpr,   // 映射表达式
    IndexExpr, // 对象表达式
    //---对象与方法---
    CallExpr,     // 函数调用表达式
    MemberExpr,   // 成员表达式
    DispatchExpr, // 异步派发表达式
    //---闭包与高级特性--
    ClosureExpr,  // 闭包表达式
    AwaitExpr,    // 等待表达式
    BorrowExpr,   // 借用表达式 (&x 或 &mut x)
    WildcardExpr, // 用于 match 语句中的 _
    //---隐式节点---
    ImplicitCastExpr, // 隐式类型转换表达式
    /*---语句---*/
    // 执行一个动作，但不产生值
    // 所谓语句，就是执行完毕后，虚拟机的操作数栈高度必须与执行前完全一致。
    // 它们的作用是改变环境状态——如声明变量，改变内存，或改变指令流（如跳转）。
    //---基础语句---
    ExpressionStmt, // 表达式语句
    AssignmentStmt, // 赋值语句
    VarDeclStmt,    // 变量声明语句
    ConstDeclStmt,  // 常量声明语句 (复用VarDecl的Payload，避免破坏12字节限制)
    BlockStmt,      // 代码块
    //---控制流---
    IfStmt,        // if语句
    LoopStmt,      // 循环语句
    MatchStmt,     // match语句
    MatchCaseStmt, // match分支语句
    //---跳转与中断---
    ContinueStmt, // continue语句 (零负载节点)
    BreakStmt,    // break语句 (零负载节点)
    ReturnStmt,   // return语句
    NockStmt,     // nock语句
    //---组件挂载与卸载---
    AttachStmt, // 挂载语句
    DetachStmt, // 取消挂载语句
    TargetStmt, // 目标语句 (ECS副作用修改)
    //---异常处理---

    /*---顶层声明---*/
    // 通常只出现在文件的最外层，用于定义数据结构和执行单元
    //---基础声明---
    FunctionDecl,    // 函数声明
    InterfaceMethod, // 接口方法声明
    StructDecl,      // 结构体声明
    EnumDecl,        // 枚举声明
    TypeAliasDecl,   // 类型别名声明
    InterfaceDecl,   // 接口定义
    ImplDecl,        // 实现声明 (impl块)

    //---NIKI特有---
    ModuleDecl,    // 模块声明
    SystemDecl,    // 系统声明
    ComponentDecl, // 组件声明
    FlowDecl,      // 流程声明
    KitsDecl,      // kits
    TagDecl,       // 标签声明
    TagGroupDecl,  // 标签组声明
    //---程序根---
    ProgramRoot, // 程序根节点
    //---错误---
    ErrorNode, // 错误声明
}; // namespace niki::syntax

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

/**
 * @brief 【定长节点索引】
 * * 意图：我们设计好每个 ASTNode 的 Payload 都是固定的（比如 16-bit）。
 * 这样只要知道“房号”，就能直接定位到 Node 的物理地址。
     +-------+-------+-------+-------+-------+------
 * * | node0 | node1 | node2 | node3 | node4 | ...
 *   +--↑----+-------+--↑----+-------+-------+------
 *     {0}这里是起始点   {2} ->这就是 ASTNodeIndex(0+ASTNodeIndex)
 */
struct ASTNodeIndex {
    uint32_t index;
    // 为了方便一些不得已而为之的特殊操作，我们留下了一个隐式的转换运算符，使未来我们可以直接将uint32_t视为ASTNodeIndex。
    operator uint32_t() const { return index; }

    bool isvalid() const { return index != ~0U; }
    static ASTNodeIndex invalid() { return {~0U}; }
};

/**
 * @brief 【变长列表索引】
 * * 意图：专门存放数量不定的数据（如：函数参数列表、Block 里的语句）。
 * 我们不只给个起点，而是直接给出“起点 + 长度”，相当于拿到了一个“数据切片”。
 * * 结构区分：
 * - 定长数据（如二元表达式）：直接存 ASTNodeIndex。
 * - 变长数据（如函数参数）：存 ASTListIndex。
 * 这样能保证 std::vector<ASTNode> 里的元素永远规整对齐。

              |<-   length:3  ->|
    ----+-----+-----------------+-----------+-----+-----
     ...| ... |  节点群 A (长度3) |  节点群 B  |节点群|...
     ...|  5  |  6  |  7  |  8  |  9  |  10 |  11 |...
    ----+-----|-----+-----+-----+-----+-----+-----+-----
              ↑start_index=6
    这就是 ASTListIndex{ .start_index = 6, .length = 3 }
 * * * 这样做的好处：
 * 1. 结构对齐：不管 A 群有 3 个还是 300 个节点，存它的“卡片”永远只占 8 字节。
 * 2. 极速切片：在代码里你可以直接通过 data[start] 到 data[start + len] 拿到整组数据。
 */
struct ASTListIndex {
    uint32_t start_index;
    uint32_t length;
    bool isvalid() const { return start_index != ~0U; }
    static ASTListIndex invalid() { return {~0U, 0}; }
};
/* 再次明确：
 * 所有的语法最终在底层都是“数据流”。
 * 将“定长索引”与“变长切片”区分开，能让编译器在处理语法树时，
 * 像收割机处理整齐的麦田一样，最大化发挥现代 CPU 的吞吐能力。
 */

// 我会给下面每个结构体画个图，方便理解。

//  胖数据结构定义

/**
 * @brief 【FunctionData】函数定义 (Fat Node Side-Table Pattern)
 * 物理大小：20 Bytes (无内存对齐碎片)
 * 统一了普通函数和接口方法签名。如果 body 无效，则为接口签名。
 *
 * [ 内存物理映射图 ]
 *
 * ASTPool::function_data (std::vector<FunctionData>)
 * +-----------------------------------------------------------------------+
 * | name_id (4B) | return_type (4B) | params (8B)        | body (4B)      |
 * | (uint32_t)   | (ASTNodeIndex)   | (ASTListIndex)     | (ASTNodeIndex) |
 * +--------------+------------------+--------------------+----------------+
 */
struct FunctionData {
    uint32_t name_id;         // 4byte: 名字
    ASTNodeIndex return_type; // 4byte: 返回值类型
    ASTListIndex params;      // 8byte: 参数列表
    ASTNodeIndex body;        // 4byte: 函数体 (如果无效，说明是接口签名或extern)
};
/**
 * @brief 【FunctionDeclPayload】函数声明负载
 * 物理大小：4 Bytes (指向胖节点表)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.func_decl
 * +------------------------+
 * | function_index (4B)    |
 * | (uint32_t)             |
 * +------------------------+
 *             |
 *             v
 *   ASTPool::function_data (std::vector<FunctionData>)
 */
struct FunctionDeclPayload {
    uint32_t function_index; // 4byte: 指向 ASTPool::functions 的索引
};

/**
 * @brief 【StructData】结构体定义 (Fat Node Side-Table Pattern)
 * 物理大小：20 Bytes (name_id 4B + names 8B + types 8B)
 *
 * [ 内存物理映射图 ]
 *
 * ASTPool::struct_data (std::vector<StructData>)
 * +---------------------------------------------------+
 * | name_id (4B) | names (8B)         | types (8B)    |
 * | (uint32_t)   | (ASTListIndex)     | (ASTListIndex)|
 * +--------------+--------------------+---------------+
 *       |                  |                  |
 *       v                  v                  v
 * ASTPool::string_pool  ASTPool::lists_elements
 * "MyStruct"            [n1, n2, n3...]      [t1, t2, t3...]
 */
struct StructData {
    uint32_t name_id;   // 4byte
    ASTListIndex names; // 8byte: 纯净的字段名列表
    ASTListIndex types; // 8byte: 纯净的字段类型列表
};
/**
 * @brief 【StructDeclPayload】结构体声明负载
 * 物理大小：4 Bytes (指向胖节点表)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.struct_decl
 * +------------------------+
 * | struct_index (4B)      |
 * | (uint32_t)             |
 * +------------------------+
 *             |
 *             v
 *   ASTPool::struct_data (std::vector<StructData>)
 */
struct StructDeclPayload {
    uint32_t struct_index; // 4byte: 指向 ASTPool::structs 的索引
};

// --- 已废弃：InterfaceMethodData 合并至 FunctionData ---
// 接口方法签名和普通函数一样，都是指向 ASTPool::function_data 的 4 字节索引。
// 区别在于：接口方法对应的 FunctionData 的 body 字段为 invalid()。

/**
 * @brief 【ImplData】实现块数据 (Fat Node Side-Table Pattern)
 * 物理大小：16 Bytes
 * 用于将外部方法挂载到指定的结构体或组件上。
 *
 * [ 内存物理映射图 ]
 * ASTPool::impl_data (std::vector<ImplData>)
 * +---------------------------------------------------+
 * | target_type (4B)   | trait_type (4B)| methods (8B)|
 * | (ASTNodeIndex)     | (ASTNodeIndex) |(ASTListIdx) |
 * +--------------------+----------------+-------------+
 *       |                  |                  |
 *       v                  v                  v
 * ASTPool::nodes     ASTPool::nodes    ASTPool::lists_elements (FunctionDecl 索引)
 */
struct ImplData {
    ASTNodeIndex target_type; // 4byte: 为哪个类型实现 (如 Player)
    ASTNodeIndex trait_type;  // 4byte: 实现了哪个接口 (无效值代表普通的方法实现)
    ASTListIndex methods;     // 8byte: 方法列表 (指向 NodeType::FunctionDecl 节点)
};
/**
 * @brief 【ImplDeclPayload】实现声明负载
 * 物理大小：4 Bytes (指向胖节点表)
 */
struct ImplDeclPayload {
    uint32_t impl_index; // 指向 ASTPool::impl_data
};

/**
 * @brief 【KitsData】上下文/组件包定义 (Fat Node Side-Table Pattern)
 * 物理大小：12 Bytes (4B 对齐，无碎片)
 *
 * [ 内存物理映射图 ]
 *
 * ASTPool::kits_data (std::vector<KitsData>)
 * +---------------------------------------+
 * | name_id (4B) | members (8B)           |
 * | (uint32_t)   | (ASTListIndex)         |
 * +--------------+------------------------+
 *       |                  |
 *       v                  v
 * ASTPool::string_pool  ASTPool::lists_elements (std::vector<ASTNodeIndex>)
 * "MyKit"               +---------------------------------------------------+
 *                       | decl_idx0 | decl_idx1 | decl_idx2 | ...           |
 *                       +---------------------------------------------------+
 *                         ^-- start_index                    ^-- start_index + length
 */
struct KitsData {
    uint32_t name_id;     // 4byte: 上下文名字
    ASTListIndex members; // 8byte: 上下文内部的影子数据或成员声明
};
/**
 * @brief 【KitsDeclPayload】Kits声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.kits_decl
 * +---------------------------------------+
 * | name_id (4B)       | body (4B)        |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes (指向 NodeType::BlockStmt)
 */
struct KitsDeclPayload {
    uint32_t name_id;  // 4byte: kits 名字
    ASTNodeIndex body; // 4byte: kits 作用域体 (BlockStmt)
};

/**
 * @brief 【MapData】映射/字典数据 (Fat Node Side-Table Pattern)
 * 物理大小：16 Bytes
 * 放弃交错列表，采用纯粹的 SOA (Structure of Arrays) 布局以优化类型检查和遍历。
 *
 * [ 内存物理映射图 ]
 * ASTPool::map_data (std::vector<MapData>)
 * +---------------------------------------+
 * | keys (8B)          | values (8B)      |
 * | (ASTListIndex)     | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::lists_elements  ASTPool::lists_elements
 * [k1, k2, k3...]          [v1, v2, v3...]
 */
struct MapData {
    ASTListIndex keys;
    ASTListIndex values;
};
/**
 * @brief 【MapExprPayload】映射表达式负载
 * 物理大小：4 Bytes (指向胖节点表)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.map
 * +------------------------+
 * | map_data_index (4B)    |
 * | (uint32_t)             |
 * +------------------------+
 *             |
 *             v
 *     ASTPool::map_data
 */
/*在设计mappayload的时候，我曾经思考要不要用交错数据存储，也就是下面这样
[key][value][key][value][key][value]...
但是想了想，感觉不如直接像function等payload一样，直接建立一个mappayload，然后使用旁侧表来管理
*/
struct MapExprPayload {
    uint32_t map_data_index; // 指向 ASTPool::map_data
};

// payload，实际用来装载节点索引和其他数据的结构体，我们所有的payload大小都被严格限制在12字节。
// 这是为了方便我们在后续的序列化中，直接将ast节点的索引和其他数据打包成16字节的结构体，然后直接写入文件。
// 这让我们每个struct的大小变得定长，这极大的方便了我们后续的序列化！同时极大的提高了cpu在扫描ast时的效率。
// 根据类型的不同，我们的每个payload结构体都有不同的字段。
// 例如，二元表达式的payload包含了操作符类型和左右操作数的索引。
/**
 * @brief 【BinaryExprPayload】二元表达式负载
 * 物理大小：12 Bytes (TokenType 1B + 3B pad + left 4B + right 4B)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.binary (12B 限制内)
 * +---------------------------------------------------+
 * | op (1B) | pad (3B) | left (4B)    | right (4B)    |
 * |(TokenType)         | (ASTNodeIndex)| (ASTNodeIndex)|
 * +--------------------+--------------+---------------+
 *                              |              |
 *                              v              v
 *                      ASTPool::nodes    ASTPool::nodes
 */
struct BinaryExprPayload {
    TokenType op;       // 1byte(底层自动补全为4byte)
    ASTNodeIndex left;  // 4byte
    ASTNodeIndex right; // 4byte
};

/**
 * @brief 【UnaryExprPayload】一元表达式负载
 * 物理大小：8 Bytes (TokenType 1B + 3B pad + operand 4B)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.unary
 * +---------------------------------------+
 * | op (1B) | pad (3B) | operand (4B)     |
 * |(TokenType)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *                              |
 *                              v
 *                      ASTPool::nodes
 */
struct UnaryExprPayload {
    TokenType op;         // 1byte(底层自动补全为4byte)
    ASTNodeIndex operand; // 4byte
};

/**
 * @brief 【LiteralExprPayload】字面量表达式负载
 * 物理大小：8 Bytes (TokenType 1B + 3B pad + const_pool_index 4B)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.literal
 * +---------------------------------------+
 * | literal_type (1B)| pad | const_idx(4B)|
 * | (TokenType)      |     | (uint32_t)   |
 * +------------------------+--------------+
 *                                 |
 *                                 v
 *                        ASTPool::constants (std::vector<vm::Value>)
 */
struct LiteralExprPayload {
    TokenType literal_type;    // 1byte(底层自动补全为4byte)
    uint32_t const_pool_index; // 4byte
};

/**
 * @brief 【IdentifierExprPayload】标识符表达式负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.identifier
 * +-------------------+
 * | name_id (4B)      |
 * | (uint32_t)        |
 * +-------------------+
 *         |
 *         v
 * ASTPool::string_pool
 */
struct IdentifierExprPayload {
    uint32_t name_id; // 4byte
};

//---复杂数据结构---

/**
 * @brief 【ArrayExprPayload】数组表达式负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.array
 * +---------------------------------------+
 * | elements (8B)                         |
 * | (ASTListIndex)                        |
 * +---------------------------------------+
 *         |
 *         v
 * ASTPool::lists_elements (std::vector<ASTNodeIndex>)
 */
struct ArrayExprPayload {
    ASTListIndex elements; // 8byte
};

/**
 * @brief 【IndexExprPayload】对象索引表达式负载 (arr[0] / map["key"])
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.index
 * +---------------------------------------+
 * | target (4B)        | index (4B)       |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct IndexExprPayload {
    ASTNodeIndex target; // 4byte: 被索引的对象 (比如 arr)
    ASTNodeIndex index;  // 4byte: 索引值 (比如 0)
};

//---对象与方法---

/**
 * @brief 【CallExprPayload】函数调用表达式负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.call
 * +---------------------------------------+
 * | callee (4B)        | arguments (8B)   |
 * | (ASTNodeIndex)     | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes   ASTPool::lists_elements
 */
struct CallExprPayload {
    ASTNodeIndex callee;    // 被调用的函数或方法
    ASTListIndex arguments; // 调用时的函数列表
};

/**
 * @brief 【MemberExprPayload】成员访问表达式负载 (obj.prop)
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.member
 * +---------------------------------------+
 * | object (4B)        | property_id (4B) |
 * | (ASTNodeIndex)     | (uint32_t)       |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes   ASTPool::string_pool
 */
struct MemberExprPayload {
    ASTNodeIndex object;  // 4byte: 对象 (比如 arr)
    uint32_t property_id; // 4byte: 属性 (比如 0 或 "key")
};

/**
 * @brief 【DispatchExprPayload】异步派发表达式负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.dispatch
 * +---------------------------------------+
 * | callee (4B)        | arguments (8B)   |
 * | (ASTNodeIndex)     | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes   ASTPool::lists_elements
 */
struct DispatchExprPayload {
    ASTNodeIndex callee;    // 被调用的函数或方法
    ASTListIndex arguments; // 调用时的函数列表
};

//---闭包与高级特性---

/**
 * @brief 【ClosureExprPayload】闭包表达式负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.closure
 * +---------------------------------------+
 * | function_decl (4B) | captures (8B)    |
 * | (ASTNodeIndex)     | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes   ASTPool::lists_elements
 */
struct ClosureExprPayload {
    ASTNodeIndex function_decl; // 4byte: 指向一个匿名的 FunctionDecl 节点
    ASTListIndex captures;      // 8byte: 捕获的外部变量列表 (Upvalues)
};

/**
 * @brief 【AwaitExprPayload】等待表达式负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.await_expr
 * +---------------------------------------+
 * | operand (4B)       | kits (4B)        |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct AwaitExprPayload {
    ASTNodeIndex operand; // 4byte: 被等待的表达式 (如任务、时间、动画)
    ASTNodeIndex kits;    // 4byte: 所属的 Kits (如在哪个队列或实体上等待)
};

/**
 * @brief 【BorrowExprPayload】借用表达式负载 (&x 或 &mut x)
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.borrow
 * +---------------------------------------+
 * | is_mut(1B) | pad (3B) | operand (4B)  |
 * | (bool)                | (ASTNodeIndex)|
 * +-----------------------+---------------+
 *                                |
 *                                v
 *                        ASTPool::nodes
 */
struct BorrowExprPayload {
    bool is_mut;          // 1byte: 是否为可变借用 (&mut)
    ASTNodeIndex operand; // 4byte: 被借用的目标 (比如 IdentifierExpr)
};

//---隐式节点---

/**
 * @brief 【ImplicitCastExprPayload】隐式类型转换表达式负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.implicit_cast
 * +---------------------------------------+
 * | operand (4B)       | to_type (4B)     |
 * | (ASTNodeIndex)     | (NKType)         |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes     (内联的NKType Handle)
 */
struct ImplicitCastExprPayload {
    ASTNodeIndex operand; // 4byte: 被转换的原始表达式
    NKType to_type;       // 4byte: 目标类型 (底层已优化为4字节的 handle)
};
/*---语句---*/
// 基础语句

/**
 * @brief 【ExpressionStmtPayload】表达式语句负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.expr_stmt
 * +-------------------+
 * | expression (4B)   |
 * | (ASTNodeIndex)    |
 * +-------------------+
 *         |
 *         v
 *   ASTPool::nodes
 */
struct ExpressionStmtPayload {
    ASTNodeIndex expression; // 4byte: 语句体
};

/**
 * @brief 【AssignmentStmtPayload】赋值语句负载
 * 物理大小：12 Bytes (TokenType 1B + 3B pad + target 4B + value 4B)
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.assign_stmt
 * +---------------------------------------------------+
 * | op (1B) | pad (3B) | target (4B)  | value (4B)    |
 * |(TokenType)         | (ASTNodeIndex)| (ASTNodeIndex)|
 * +--------------------+--------------+---------------+
 *                              |              |
 *                              v              v
 *                      ASTPool::nodes    ASTPool::nodes
 */
struct AssignmentStmtPayload {
    TokenType op;        // 1byte(自动补全为4byte): =, +=, -= 等
    ASTNodeIndex target; // 4byte: 左侧被赋值的目标 (IdentifierExpr, IndexExpr, MemberExpr 等)
    ASTNodeIndex value;  // 4byte: 右侧的值表达式
};

/**
 * @brief 【VarDeclStmtPayload】变量声明语句负载
 * 物理大小：12 Bytes
 * 注意：ConstDeclStmt 完美复用此 Payload，依靠外部 NodeType 区分语义。
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.var_decl
 * +---------------------------------------------------+
 * | name_id (4B)       | type_expr(4B)| init_expr(4B) |
 * | (uint32_t)         | (ASTNodeIndex)| (ASTNodeIndex)|
 * +--------------------+--------------+---------------+
 *         |                    |              |
 *         v                    v              v
 * ASTPool::string_pool   ASTPool::nodes ASTPool::nodes
 */
struct VarDeclStmtPayload {
    uint32_t name_id;       // 4byte:变量名的字符串池 ID
    ASTNodeIndex type_expr; // 4byte:类型标注的AST节点索引 (通常是一个 IdentifierExpr 或 CallExpr)
    ASTNodeIndex init_expr; // 4byte:初始化表达式的AST节点索引
};

/**
 * @brief 【BlockStmtPayload】代码块语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.block
 * +---------------------------------------+
 * | statements (8B)                       |
 * | (ASTListIndex)                        |
 * +---------------------------------------+
 *         |
 *         v
 * ASTPool::lists_elements (std::vector<ASTNodeIndex>)
 */
struct BlockStmtPayload {
    ASTListIndex statements; // 8byte: 语句列表
};

//---控制流---

/**
 * @brief 【IfStmtPayload】If语句负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.if_stmt
 * +---------------------------------------------------+
 * | condition (4B)     | then_branch(4B)| else_branch(4B)|
 * | (ASTNodeIndex)     | (ASTNodeIndex)| (ASTNodeIndex)|
 * +--------------------+--------------+---------------+
 *         |                    |              |
 *         v                    v              v
 *   ASTPool::nodes       ASTPool::nodes ASTPool::nodes
 */
struct IfStmtPayload {
    ASTNodeIndex condition;   // 4byte: 条件表达式
    ASTNodeIndex then_branch; // 4byte: 然后分支
    ASTNodeIndex else_branch; // 4byte: 否则分支
};

/**
 * @brief 【LoopStmtPayload】循环语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.loop
 * +---------------------------------------+
 * | condition (4B)     | body (4B)        |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct LoopStmtPayload {
    ASTNodeIndex condition; // 4byte: 循环条件表达式
    ASTNodeIndex body;      // 4byte: 循环体
};

/**
 * @brief 【MatchStmtPayload】Match语句负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.match
 * +---------------------------------------+
 * | expression (4B)    | cases (8B)       |
 * | (ASTNodeIndex)     | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes   ASTPool::lists_elements
 */
struct MatchStmtPayload {
    ASTNodeIndex expression; // 4byte: 匹配表达式
    ASTListIndex cases;      // 8byte: 匹配分支列表
};

/**
 * @brief 【MatchCaseStmtPayload】Match分支语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.match_case
 * +---------------------------------------+
 * | pattern (4B)       | body (4B)        |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct MatchCaseStmtPayload {
    ASTListIndex pattern; // 4byte: 匹配模式
    ASTNodeIndex body;    // 4byte: 匹配体
};

//---跳转与中断---

/**
 * @brief 【ReturnStmtPayload】Return语句负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.return_stmt
 * +-------------------+
 * | expression (4B)   |
 * | (ASTNodeIndex)    |
 * +-------------------+
 *         |
 *         v
 *   ASTPool::nodes
 */
struct ReturnStmtPayload {
    ASTNodeIndex expression; // 4byte: 返回值表达式
};

/**
 * @brief 【NockStmtPayload】Nock(让出/休眠)语句负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.nock
 * +-------------------+
 * | interval (4B)     |
 * | (ASTNodeIndex)    |
 * +-------------------+
 *         |
 *         v
 *   ASTPool::nodes
 */
struct NockStmtPayload {
    ASTNodeIndex interval; // 4byte: 等待时间间隔 (以帧/Tick为单位)
};

//---组件挂载与卸载---

/**
 * @brief 【AttachStmtPayload】挂载语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.attach
 * +---------------------------------------+
 * | component (4B)     | target (4B)      |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct AttachStmtPayload {
    ASTNodeIndex component; // 4byte: 组件
    ASTNodeIndex target;    // 4byte: 目标实体（1或多个）
};

/**
 * @brief 【DetachStmtPayload】卸载语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.detach
 * +---------------------------------------+
 * | component (4B)     | target (4B)      |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct DetachStmtPayload {
    ASTNodeIndex component; // 4byte: 组件
    ASTNodeIndex target;    // 4byte: 目标实体（1或多个）
};

// 目标副作用语句

/**
 * @brief 【TargetStmtPayload】目标(副作用)语句负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.target_stmt
 * +---------------------------------------+
 * | targets (4B)       | body (4B)        |
 * | (ASTNodeIndex)     | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 *   ASTPool::nodes       ASTPool::nodes
 */
struct TargetStmtPayload {
    ASTNodeIndex targets; // 4byte: 目标对象（通常是带Tag的Query表达式）
    ASTNodeIndex body;    // 4byte: 作用域内的代码块，用于产生副作用
};

//---异常处理---

/*---顶层声明---*/
//---基础声明---

/**
 * @brief 【EnumDeclPayload】枚举声明负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.enum_decl
 * +------------------------+
 * | enum_data (4B)         |
 * | (ASTNodeIndex)         |
 * +------------------------+
 *             |
 *             v
 *       ASTPool::nodes (通常指向 ArrayExpr)
 */
struct EnumDeclPayload {
    ASTNodeIndex enum_data; // 4byte: 枚举数据（指向一个Array节点）
};

/**
 * @brief 【TypeAliasDeclPayload】类型别名声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.type_alias
 * +---------------------------------------+
 * | name_id (4B)       | type_expr (4B)   |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes
 */
struct TypeAliasDeclPayload {
    uint32_t name_id;       // 4byte: 类型别名名的字符串池 ID
    ASTNodeIndex type_expr; // 4byte: 类型表达式的AST节点索引
};

/**
 * @brief 【InterfaceDeclPayload】接口声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.interface_decl
 * +---------------------------------------+
 * | name_id (4B)       | body (4B)        |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes (指向 NodeType::BlockStmt)
 */
struct InterfaceDeclPayload {
    uint32_t name_id;  // 4byte: 接口名的字符串池 ID
    ASTNodeIndex body; // 4byte: 接口体 (BlockStmt)
};

//---NIKI特有---

/**
 * @brief 【ModuleDeclPayload】模块声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.module_decl
 * +---------------------------------------+
 * | name_id (4B)       | body (4B)        |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes (指向 NodeType::BlockStmt)
 */
struct ModuleDeclPayload {
    uint32_t name_id;  // 4byte: 模块名的字符串池 ID
    ASTNodeIndex body; // 4byte: 模块体 (BlockStmt)
};

/**
 * @brief 【SystemDeclPayload】系统声明负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.system_decl
 * +---------------------------------------------------+
 * | name_id (4B)       | body (4B)    | system_data(4B)|
 * | (uint32_t)         | (ASTNodeIndex)| (ASTNodeIndex)|
 * +--------------------+--------------+---------------+
 *         |                    |              |
 *         v                    v              v
 * ASTPool::string_pool   ASTPool::nodes ASTPool::nodes (Query)
 */
struct SystemDeclPayload {
    uint32_t name_id;         // 4byte: 系统名的字符串池 ID
    ASTNodeIndex body;        // 4byte: 系统体
    ASTNodeIndex system_data; // 4byte: 纯函数的依赖声明(Query)
};

/**
 * @brief 【ComponentDeclPayload】组件声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.component_decl
 * +---------------------------------------+
 * | name_id (4B)       | body (4B)        |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes
 */
struct ComponentDeclPayload {
    uint32_t name_id;  // 4byte: 组件名的字符串池 ID
    ASTNodeIndex body; // 4byte: 组件体
};

/**
 * @brief 【FlowDeclPayload】流程声明负载
 * 物理大小：8 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.flow_decl
 * +---------------------------------------+
 * | name_id (4B)       | body (4B)        |
 * | (uint32_t)         | (ASTNodeIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::nodes
 */
struct FlowDeclPayload {
    uint32_t name_id;  // 4byte: 流程名的字符串池 ID
    ASTNodeIndex body; // 4byte: 流程体
};

/**
 * @brief 【TagDeclPayload】标签声明负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.tag_decl
 * +-------------------+
 * | name_id (4B)      |
 * | (uint32_t)        |
 * +-------------------+
 *         |
 *         v
 * ASTPool::string_pool
 */
struct TagDeclPayload {
    uint32_t name_id; // 4byte: 标签名的字符串池 ID
};

/**
 * @brief 【TagGroupDeclPayload】标签组声明负载
 * 物理大小：12 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.tag_group
 * +---------------------------------------+
 * | name_id (4B)       | tags (8B)        |
 * | (uint32_t)         | (ASTListIndex)   |
 * +--------------------+------------------+
 *         |                    |
 *         v                    v
 * ASTPool::string_pool   ASTPool::lists_elements
 */
struct TagGroupDeclPayload {
    uint32_t name_id;  // 4byte: 标签组名的字符串池 ID
    ASTListIndex tags; // 8byte: 标签列表
};

//---程序根---

/**
 * @brief 【ListPayload】通用列表包裹负载
 * 物理大小：8 Bytes
 * 用于所有“仅包裹一个节点列表”的 AST 节点，以减少 Payload 结构体的无意义重复。
 * 适用节点：BlockStmt, ArrayExpr, ProgramRoot 等。
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.list
 * +---------------------------------------+
 * | elements (8B)                         |
 * | (ASTListIndex)                        |
 * +---------------------------------------+
 */
struct ListPayload {
    ASTListIndex elements;
};

//---错误---

/**
 * @brief 【ErrorPayload】错误节点负载
 * 物理大小：4 Bytes
 *
 * [ 内存物理映射图 ]
 * ASTNodePayload.error
 * +------------------------+
 * | error_index (4B)       |
 * | (uint32_t)             |
 * +------------------------+
 *             |
 *             v
 *   (未来指向 ErrorPool 或类似结构)
 */
struct ErrorPayload {
    uint32_t error_index; // 4byte: 指向 ASTPool::errors 的索引
};

union ASTNodePayload {
    //---表达式---
    BinaryExprPayload binary;
    UnaryExprPayload unary;
    LiteralExprPayload literal;
    IdentifierExprPayload identifier;
    //---复杂数据结构---
    // ArrayExprPayload 合并至 ListPayload
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
    // --- 新增：类型检查期的旁侧表 ---
    // 为什么不在 ASTNode 里加 NKType？因为会破坏 16 字节对齐，导致体积膨胀 50%！
    // 使用 DOD 的旁侧表模式，与 nodes 同步增长，实现缓存友好的按需遍历。
    std::vector<NKType> node_types;

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
    std::vector<std::string> string_pool;
    std::unordered_map<std::string_view, uint32_t> string_to_id;

    // 我们使用std::span提供了一个get_list方法，为的是方便我们获取一个列表的所有元素索引，这在我们需要遍历一个列表时非常方便。
    // std::span相当于一个指向数组的视图，只要我们提供这个数组的起始地址和长度，它就会返回一个指向这个数组的子数组的视图。
    // 注意！std::span不负责管理内存，是只读的，我们不能用它来修改数组的内容。这样同时导致其是安全和高性能的。
    std::span<const ASTNodeIndex> get_list(ASTListIndex list_info) const;
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
