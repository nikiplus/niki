#pragma once

#include "niki/semantic/nktype.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <span>
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
    CallExpr,   // 函数调用表达式
    MemberExpr, // 成员表达式
    InvokeExpr, // 调用表达式
    //---闭包与高级特性--
    ClosureExpr, // 闭包表达式
    AwaitExpr,   // 等待表达式
    //---隐式节点---
    ImplicitCastExpr, // 隐式类型转换表达式
    /*---语句---*/
    // 执行一个动作，但不产生值
    // 所谓语句，就是执行完毕后，虚拟机的操作数栈高度必须与执行前完全一致。
    // 它们的作用是改变环境状态——如声明变量，改变内存，或改变指令流（如跳转）。
    //---基础语句---
    ExpressionStmt, // 表达式语句
    VarDeclStmt,    // 变量声明语句
    BlockStmt,      // 代码块
    //---控制流---
    IfStmt,        // if语句
    LoopStmt,      // 循环语句
    MatchStmt,     // match语句
    MatchCaseStmt, // match分支语句
    //---跳转与中断---
    // ContinueStmt, // continue语句
    // BreakStmt,    // break语句
    ReturnStmt, // return语句
    NockStmt,   // nock语句
    //---组件挂载与卸载---
    AttachStmt, // 挂载语句
    DetachStmt, // 取消挂载语句
    TargetStmt, // 目标语句 (ECS副作用修改)
    //---异常处理---
    ThrowStmt,    // throw语句
    TryCatchStmt, // try-catch语句

    /*---顶层声明---*/
    // 通常只出现在文件的最外层，用于定义数据结构和执行单元
    //---基础声明---
    FunctionDecl,  // 函数声明
    StructDecl,    // 结构体声明
    EnumDecl,      // 枚举声明
    TypeAliasDecl, // 类型别名声明
    InterfaceDecl, // 接口定义
    //---NIKI特有---
    ModuleDecl,    // 模块声明
    SystemDecl,    // 系统声明
    ComponentDecl, // 组件声明
    FlowDecl,      // 流程声明
    ContextDecl,   // 上下文声明 (数据隔离作用域)
    TagDecl,       // 标签声明
    TagGroupDecl,  // 标签组声明
    //---程序根---
    ProgramRoot, // 程序根节点
    //---错误---
    ErrorNode, // 错误声明
};

// 这里我们通过使用struct包裹uint32_t，来完成了对该类数据类型的类型定义
// 若无struct包裹，我们会直接使用
// using ASTNodeIndex = uint32_t;
// 当我们使用上面缩写的using而非struct包裹，我们将在代码里看到大量的~0u,而这会导致代码的可读性下降。
// 同时，在实际操作时我们有可能把一个无效的索引值（如~0U）错误地解释为一个有效索引。甚至有可能让使用者错误的对索引进行值操作。
struct ASTNodeIndex {
    uint32_t index;
    // 为了方便一些不得已而为之的特殊操作，我们留下了一个隐式的转换运算符，使未来我们可以直接将uint32_t视为ASTNodeIndex。
    operator uint32_t() const { return index; }

    bool isvalid() const { return index != ~0U; }
    static ASTNodeIndex invalid() { return {~0U}; }
};

// 再次明确，所有的语法最终在底层都会被压缩为数据，而只要是数据，我们就能用索引访问。
// 我们思考这样一个结构，通过设计指北，我们已知，ast语法树是一个树形结构，每一个父节点要包含多个子节点的地址。
// 那么我们第一时间想到的肯定是——欸？那我直接用C++的指针不就好了？这样a对象直接访问bc对象，不久完成了索引吗？
// 道理是这样没错，但指针和struct空间的占用是有开销的——若未提前声明，struct在内存上的存储是散乱的，这进而导致指针的访问也变得散乱。
// 那如果我们不用 new ，而是直接把节点塞进 vector<ASTNode> 里让内存连续，我们能用物理指针去互相指向对方吗？
// 答案是否定的，因为vector虽然连续了，但依旧会导致下述问题。
// 1.指针会随着内存的跳动而失效——vector在内存空间不足时会在内存上找一块更大的空间，把原来的struct整体搬迁过去，这会导致所有指向原内存的指针都失效。
// 2.索引通常比指针更“瘦”，在64位系统上，我们的索引Uint32_t占用4字节，而指针占用8字节。
// 3.索引是相对坐标，相比起指针，它更易序列化——我们只需要存储整个vector，就可以同时存储struct之间的关系。
// 那么得知了这一切，我们接下来就可以开始设计我们的ast节点了。
// 我们来设计一个专门用来存放边长列表(如函数参数/block语句等)的结构体。
// 可以看到，除了名字之外，该结构体和上面的ASTNodeIndex几乎没有任何区别。
// 它的存在主要是为了方便我们在代码中对列表进行操作。
// 比如我们可以用ASTListIndex来表示一个函数的参数列表，用ASTListIndex来表示一个block语句的语句列表等，这样的设计可以有效避免后期使用时可能的混淆。
// 同时，要注意，我们这里会存储两类数据，一类是定长的，如二元表达式的左右操作数，另一类是变长的，如函数参数/block语句等。
// 对这两类数据进行区分，能有效保证astnode结构大小的固定性，这让后面的std::vector<ASTNode>中的数据类型几位规整。
// 我们直接提供了start_index和length来表示列表的起始索引和长度，这让我们在访问列表数据时可以直接获得整个数据切片，而非索引起点。
struct ASTListIndex {
    uint32_t start_index;
    uint32_t length;
    bool isvalid() const { return start_index != ~0U; }
    static ASTListIndex invalid() { return {~0U, 0}; }
};

// 胖数据结构定义
struct FunctionData {
    uint32_t name_id;    // 4byte
    ASTListIndex params; // 8byte
    ASTNodeIndex body;   // 4byte
};
struct StructData {
    uint32_t name_id;    // 4byte
    ASTListIndex fields; // 8byte:交错列表 [name, type, name, type...]
};
struct ContextData {
    uint32_t name_id;     // 4byte: 上下文名字
    ASTListIndex members; // 8byte: 上下文内部的影子数据或成员声明
};
// payload，实际用来装载节点索引和其他数据的结构体，我们所有的payload大小都被严格限制在12字节。
// 这是为了方便我们在后续的序列化中，直接将ast节点的索引和其他数据打包成16字节的结构体，然后直接写入文件。
// 这让我们每个struct的大小变得定长，这极大的方便了我们后续的序列化！同时极大的提高了cpu在扫描ast时的效率。
// 根据类型的不同，我们的每个payload结构体都有不同的字段。
// 例如，二元表达式的payload包含了操作符类型和左右操作数的索引。
struct BinaryExprPayload {
    // 我们要进行一下每个字段的数据大小标记和计算。
    TokenType op;       // 1byte(底层自动补全为4byte)
    ASTNodeIndex left;  // 4byte
    ASTNodeIndex right; // 4byte
    // 4+4+4=12byte
};
struct UnaryExprPayload {
    TokenType op;         // 1byte(底层自动补全为4byte)
    ASTNodeIndex operand; // 4byte
    // 4+4=8byte
};
struct LiteralExprPayload {
    TokenType literal_type;    // 1byte(底层自动补全为4byte)
    uint32_t const_pool_index; // 4byte
    // 4+4=8byte
};
struct IdentifierExprPayload {
    uint32_t name_id; // 4byte
    // 4byte
};
//---复杂数据结构---
struct ArrayExprPayload {
    ASTListIndex elements; // 8byte
};

struct MapExprPayload {
    // 为什么不存 keys 和 values 两个 ASTListIndex？
    // 因为 8 + 8 = 16 字节！它会直接撑爆我们定好的 12 字节 Payload 上限！
    // 解决方案：使用单一的交错列表 (Interleaved List)。
    // 在 lists_elements 数组中，我们连续存放 [key1, value1, key2, value2...]
    // 这样只需要 1 个 ASTListIndex (8 字节)，完美塞入 12 字节限制中。
    // 在读取时，只需校验 length 必须是偶数即可。
    ASTListIndex entries;
};

// 对象索引表达式（比如 arr[0] 或 map["key"]）
struct IndexExprPayload {
    ASTNodeIndex target; // 4byte: 被索引的对象 (比如 arr)
    ASTNodeIndex index;  // 4byte: 索引值 (比如 0)
    // 4 + 4 = 8byte
};

//---对象与方法---
struct CallExprPayload {
    ASTNodeIndex callee;    // 被调用的函数或方法
    ASTListIndex arguments; // 调用时的函数列表
};
struct MemberExprPayload {
    ASTNodeIndex object;  // 4byte: 对象 (比如 arr)
    uint32_t property_id; // 4byte: 属性 (比如 0 或 "key")
    // 4 + 4 = 8byte
};
struct InvocationExprPayload {
    ASTNodeIndex callee;    // 被调用的函数或方法
    ASTListIndex arguments; // 调用时的函数列表
};
//---闭包与高级特性---
struct ClosureExprPayload {
    ASTNodeIndex function_decl; // 4byte: 指向一个匿名的 FunctionDecl 节点
    ASTListIndex captures;      // 8byte: 捕获的外部变量列表 (Upvalues)
    // 4 + 8 = 12byte
};
struct AwaitExprPayload {
    ASTNodeIndex operand; // 4byte: 被等待的表达式 (如任务、时间、动画)
    ASTNodeIndex context; // 4byte: 上下文 (如在哪个队列或实体上等待)
    // 4 + 4 = 8byte
};
//---隐式节点---
struct ImplicitCastExprPayload {
    ASTNodeIndex operand; // 4byte: 被转换的原始表达式
    NKType to_type;       // 8byte: 目标类型 (必须是完整类型，不能只是BaseType)
    // 4 + 8 = 12byte
};
/*---语句---*/
// 基础语句
struct ExpressionStmtPayload {
    ASTNodeIndex expression; // 4byte: 语句体
};
struct VarDeclStmtPayload {
    uint32_t name_id;       // 4byte:变量名的字符串池 ID
    ASTNodeIndex type_expr; // 4byte:类型标注的AST节点索引 (通常是一个 IdentifierExpr 或 CallExpr)
    ASTNodeIndex init_expr; // 4byte:初始化表达式的AST节点索引
};
struct BlockStmtPayload {
    ASTListIndex statements; // 8byte: 语句列表
};
//---控制流---
struct IfStmtPayload {
    ASTNodeIndex condition;   // 4byte: 条件表达式
    ASTNodeIndex then_branch; // 4byte: 然后分支
    ASTNodeIndex else_branch; // 4byte: 否则分支
};
struct LoopStmtPayload {
    ASTNodeIndex condition; // 4byte: 循环条件表达式
    ASTNodeIndex body;      // 4byte: 循环体
};
struct MatchStmtPayload {
    ASTNodeIndex expression; // 4byte: 匹配表达式
    ASTListIndex cases;      // 8byte: 匹配分支列表
    // 4 + 8 = 12byte
};
struct MatchCaseStmtPayload {
    ASTNodeIndex pattern; // 4byte: 匹配模式
    ASTNodeIndex body;    // 4byte: 匹配体
    // 4 + 4 = 8byte
};
//---跳转与中断---
struct ReturnStmtPayload {
    ASTNodeIndex expression; // 4byte: 返回值表达式
};
struct NockStmtPayload {
    ASTNodeIndex condition; // 4byte: 等待条件
    ASTNodeIndex interval;  // 4byte: 等待时间间隔
};
//---组件挂载与卸载---
struct AttachStmtPayload {
    ASTNodeIndex component; // 4byte: 组件
    ASTNodeIndex target;    // 4byte: 目标实体（1或多个）
};
struct DetachStmtPayload {
    ASTNodeIndex component; // 4byte: 组件
    ASTNodeIndex target;    // 4byte: 目标实体（1或多个）
};
// 目标副作用语句
struct TargetStmtPayload {
    ASTNodeIndex targets; // 4byte: 目标对象（通常是带Tag的Query表达式）
    ASTNodeIndex body;    // 4byte: 作用域内的代码块，用于产生副作用
};
//---异常处理---
struct ThrowStmtPayload {
    ASTNodeIndex expression; // 4byte: 异常值表达式
};
struct TryStmtPayload {
    ASTNodeIndex try_body;   // 4byte: 尝试执行的代码块
    ASTNodeIndex catch_body; // 4byte: 捕获异常后的处理代码块
};
/*---顶层声明---*/
//---基础声明---
struct FunctionDeclPayload {
    uint32_t function_index; // 4byte: 指向 ASTPool::functions 的索引
};
struct StructDeclPayload {
    uint32_t struct_index; // 4byte: 指向 ASTPool::structs 的索引
};
struct EnumDeclPayload {
    ASTNodeIndex enum_data; // 4byte: 枚举数据（指向一个Array节点）
};
struct TypeAliasDeclPayload {
    uint32_t name_id;       // 4byte: 类型别名名的字符串池 ID
    ASTNodeIndex type_expr; // 4byte: 类型表达式的AST节点索引
};
struct InterfaceDeclPayload {
    uint32_t name_id;     // 4byte: 接口名的字符串池 ID
    ASTListIndex methods; // 8byte: 方法列表（指向一个Array节点）
};
//---NIKI特有---
struct ModuleDeclPayload {
    uint32_t name_id;     // 4byte: 模块名的字符串池 ID
    ASTListIndex exports; // 8byte: 导出列表
};
struct SystemDeclPayload {
    uint32_t name_id;         // 4byte: 系统名的字符串池 ID
    ASTNodeIndex body;        // 4byte: 系统体
    ASTNodeIndex system_data; // 4byte: 纯函数的依赖声明(Query)
};
struct ComponentDeclPayload {
    uint32_t name_id;  // 4byte: 组件名的字符串池 ID
    ASTNodeIndex body; // 4byte: 组件体
};
struct FlowDeclPayload {
    uint32_t name_id;  // 4byte: 流程名的字符串池 ID
    ASTNodeIndex body; // 4byte: 流程体
};
struct ContextDeclPayload {
    uint32_t context_index; // 4byte: 指向 ASTPool::context_data 的索引
};
struct TagDeclPayload {
    uint32_t name_id; // 4byte: 标签名的字符串池 ID
};
struct TagGroupDeclPayload {
    uint32_t name_id;  // 4byte: 标签组名的字符串池 ID
    ASTListIndex tags; // 8byte: 标签列表
};
//---程序根---
struct ProgramRootPayload {
    ASTListIndex declarations; // 8byte: 声明列表
};
//---错误---
struct ErrorPayload {
    uint32_t error_index; // 4byte: 指向 ASTPool::errors 的索引
};

// 使用union来存储不同类型的 payload,这是一种比较常用的设计模式。
//  它的存在主要是为了避免我们在设计ast节点时，为了支持不同类型的payload而导致的代码冗余。
//  同时，union的大小是由其内部最大的类型来决定的，这一特性让我们保证payload的定长——这是极便于cpu的访问的
// 已知我们上面最长的payload是12byte，那么我们的union就会占用12byte的空间。
union ASTNodePayload {
    //---表达式---
    BinaryExprPayload binary;
    UnaryExprPayload unary;
    LiteralExprPayload literal;
    IdentifierExprPayload identifier;
    ArrayExprPayload array;
    MapExprPayload map;
    IndexExprPayload index;
    CallExprPayload call;
    MemberExprPayload member;
    InvocationExprPayload invocation;
    ClosureExprPayload closure;
    AwaitExprPayload await_expr;
    ImplicitCastExprPayload implicit_cast;

    //---语句---
    TargetStmtPayload target_stmt;
    ExpressionStmtPayload expr_stmt;
    VarDeclStmtPayload var_decl;
    BlockStmtPayload block;
    IfStmtPayload if_stmt;
    LoopStmtPayload loop;
    MatchStmtPayload match;
    MatchCaseStmtPayload match_case;
    ReturnStmtPayload return_stmt;
    NockStmtPayload nock;
    AttachStmtPayload attach;
    DetachStmtPayload detach;
    ThrowStmtPayload throw_stmt;
    TryStmtPayload try_stmt;

    //---顶层声明---
    FunctionDeclPayload func_decl;
    StructDeclPayload struct_decl;
    EnumDeclPayload enum_decl;
    TypeAliasDeclPayload type_alias;
    InterfaceDeclPayload interface_decl;
    ModuleDeclPayload module_decl;
    SystemDeclPayload system_decl;
    ComponentDeclPayload component_decl;
    FlowDeclPayload flow_decl;
    ContextDeclPayload context_decl;
    TagDeclPayload tag_decl;
    TagGroupDeclPayload tag_group;
    ProgramRootPayload program_root;
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

    // 前端常量池（Side-buffer）：
    // 为了保证 ASTNode 严格维持 16 字节大小（4字节对齐），我们绝对不能把包含 8 字节对齐（double/void*）的 vm::Value
    // 直接塞进 ASTNodePayload。 这会导致整个 ASTNode 被迫 8 字节对齐，并产生 4 字节 padding，体积膨胀 50%（从 16 变成
    // 24 字节）。 因此，解析期遇到的所有字面量，都会被打包成 Value 存放在这里，AST 中仅保留 4 字节的 const_pool_index。
    std::vector<vm::Value> constants;

    std::vector<FunctionData> function_data; // 函数数据
    std::vector<StructData> struct_data;     // 结构体数据
    std::vector<ContextData> context_data;   // 上下文数据

    // 我们使用std::span提供了一个get_list方法，为的是方便我们获取一个列表的所有元素索引，这在我们需要遍历一个列表时非常方便。
    // std::span相当于一个指向数组的视图，只要我们提供这个数组的起始地址和长度，它就会返回一个指向这个数组的子数组的视图。
    // 注意！std::span不负责管理内存，是只读的，我们不能用它来修改数组的内容。这样同时导致其是安全和高性能的。
    std::span<const ASTNodeIndex> get_list(ASTListIndex list_info) const {
        if (!list_info.isvalid() || list_info.length == 0) {
            return {};
        }
        return {lists_elements.data() + list_info.start_index, list_info.length};
    }
    // 调用ASTListIndex，装载指定区域的astnode，并返回对应的astlist切片。
    ASTListIndex allocateList(std::span<const ASTNodeIndex> elements) {
        uint32_t start_index = static_cast<uint32_t>(lists_elements.size());
        // 这里之所以不使用for循环，而是使用std::vector::insert,是因为insert方法会预先计算elements的大小，使vector只需进行一次内存再分配即可容纳所有元素。
        // 而如果使用push_back，每当存储的elements量超出vector当前申请的内存大小，便会使std::vector在底层使用memove进行内存的批量拷贝，这是极消耗性能的
        lists_elements.insert(lists_elements.end(), elements.begin(), elements.end());
        return ASTListIndex{start_index, static_cast<uint32_t>(elements.size())};
    };
    //---辅助函数---
    void clear() {
        nodes.clear();
        locations.clear();
        lists_elements.clear();
        constants.clear();
        function_data.clear();
        struct_data.clear();
    };
};

} // namespace niki::syntax
