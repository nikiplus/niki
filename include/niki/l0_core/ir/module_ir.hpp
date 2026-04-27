#pragma once

#include "niki/l0_core/semantic/nktype.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
namespace niki::ir {

/**
 * @brief IR（Intermediate Representation）数据模型总览
 *
 * 这份头文件定义的是编译器在“语义分析之后、目标字节码之前”的内部语言。
 *
 * Why（为什么需要 IR）:
 * - AST 擅长表达“语法形状”，不擅长直接承载后端执行语义。
 * - 后端需要稳定、可验证、可降级（lowering）的结构化表示。
 * - Linker 需要显式符号信息，不能长期依赖常量池扫描推断。
 *
 * How（怎么组织）:
 * - 用“模块 -> 函数 -> 基本块 -> 指令 -> 值”的层级组织 IR。
 * - 用 ID（uint32_t）替代裸指针，保证跨容器与跨阶段的稳定引用。
 * - 用统一 IRValue 承载寄存器、立即数、符号、块目标等多种操作数。
 *
 * 全景图：
 * ModuleIR
 *  ├─ function_table (IRFunction[])
 *  │   └─ basic_blocks (IRBasicBlock[])
 *  │      └─ instruction_list (IRInst[])
 *  │         └─ operands (IRValue)
 *  ├─ symbol_table (IRSymbol[])
 *  └─ module_string_pool (string[])
 */

//---基础ID类型---
// Why:
// - IR 在 builder/verify/lowering/linker 多阶段流转，需要稳定、可序列化、可比较的身份标识。
// - 使用 uint32_t 而非指针，避免生命周期耦合与跨容器失效问题。
// How:
// - 统一采用“索引即 ID”模型：对象存于 vector，ID 为其下标。
using IRFunctionId = uint32_t;
using IRBlockId = uint32_t;
using IRRegId = uint32_t;
using IRSymbolId = uint32_t;

//---类型系统(IR)侧---
// MVP阶段直接复用semantic::NKType
// Why:
// - 避免 AST/语义/IR 三层重复维护类型定义，减少类型漂移风险。
// - 降低迁移成本：先跑通 IR-first 主链，再按需拆分专用 IRType。
using IRType = semantic::NKType;

//---IRValue---
// Why:
// - 后端需要统一表示“值来源”：寄存器、立即数、符号、控制流目标。
// - 统一值模型可让 verify/lowering 用相同规则处理操作数合法性。
// How:
// - value_kind 决定 payload 的解释方式（tagged payload）。
// - 这样所有指令都能使用统一的操作数字段，不需要为每种值单独建结构。
enum class IRValueKind : uint8_t {
    Invalid = 0,
    VReg,     // 虚拟寄存器
    ImmI64,   // 立即数整数
    ImmF64,   // 立即数浮点(bit-case 存储)
    ImmBool,  // 立即数布尔值(payload_u32:0/1)
    StringId, // 字符池ID
    SymbolId, // 符号ID(模块内/跨模块)
    BlockId,  // 基本块ID(跳转目标)
    FuncId,   // 函数ID(直接调用目标)
};

struct IRValue {
    // Why:
    // - 运行时并不知道这个字段的具体语义，解释权由 value_kind 决定。
    // - 这是一个“tagged payload”模型：小而稳定，便于跨阶段传递。
    IRValueKind value_kind = IRValueKind::Invalid;

    // 统一payload，按value_kind解释
    uint32_t payload_as_u32 = 0;
    int64_t payload_as_i64 = 0;
    uint64_t payload_as_u64 = 0;

    static IRValue makeInvalid() { return {}; }

    // Why:
    // - 工厂函数保证“kind 与 payload”总是成对正确写入，避免手写错配。
    // How:
    // - 每个构造函数只设置自身所需字段，未使用字段保持默认值。
    // - 例如 makeVirtualRegisterValue() 只写 VReg + payload_as_u32。
    static IRValue makeVirtualRegisterValue(IRRegId register_id) {
        IRValue value;
        value.value_kind = IRValueKind::VReg;
        value.payload_as_u32 = register_id;
        return value;
    };

    static IRValue makeImmediateIntegerValue(int64_t integer_value) {
        IRValue value;
        value.value_kind = IRValueKind::ImmI64;
        value.payload_as_i64 = integer_value;
        return value;
    };

    static IRValue makeImmediateFloatBitValue(uint64_t float_bit_pattern) {
        IRValue value;
        value.value_kind = IRValueKind::ImmF64;
        value.payload_as_u64 = float_bit_pattern;
        return value;
    };

    static IRValue makeImmediateBooleanValue(bool boolean_value) {
        IRValue value;
        value.value_kind = IRValueKind::ImmBool;
        value.payload_as_u32 = boolean_value ? 1u : 0u;
        return value;
    };

    static IRValue makeStringIdentifierValue(uint32_t string_identifier) {
        IRValue value;
        value.value_kind = IRValueKind::StringId;
        value.payload_as_u32 = string_identifier;
        return value;
    };

    static IRValue makeSymbolIdentifierValue(IRSymbolId symbol_identifier) {
        IRValue value;
        value.value_kind = IRValueKind::SymbolId;
        value.payload_as_u32 = symbol_identifier;
        return value;
    };

    static IRValue makeBlockIdentifierValue(IRBlockId block_identifier) {
        IRValue value;
        value.value_kind = IRValueKind::BlockId;
        value.payload_as_u32 = block_identifier;
        return value;
    };

    static IRValue makeFunctionIdentifierValue(IRFunctionId function_identifier) {
        IRValue value;
        value.value_kind = IRValueKind::FuncId;
        value.payload_as_u32 = function_identifier;
        return value;
    };

    bool isInvalid() const { return value_kind == IRValueKind::Invalid; }
};

//---指令---
// Why:
// - IRInstKind 应表示“后端语义原语”，而非“语法树节点”。
// - 语法多样性在 builder 收敛；IR 保持小而稳定，利于验证与降级。
// How:
// - 例如 if/while/match 这类语法结构，最终会展开为 Branch/Jump/BasicBlock 组合。
enum class IRInstKind : uint8_t {
    Nop = 0,
    // 常量与数据搬运
    Constant,
    Move,
    // 算术/比较/逻辑
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,

    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,

    LogicAnd,
    LogicOr,
    LogicNot,

    // 变量/符号访问
    LoadGlobal,
    StoreGlobal,

    // 调用
    Call,
    Return,

    // 控制流
    Jump,   // 无条件跳转
    Branch, // 条件跳转

    // 占位
    Phi
};

struct IRInst {
    // Why:
    // - 指令模型采用固定槽位(目标+3操作数)，简化数据结构与遍历逻辑。
    // - 不同指令对槽位的解释由 instruction_kind 决定。
    // How:
    // - verify 负责检查“当前 kind 是否正确使用了这些槽位”。
    IRInstKind instruction_kind = IRInstKind::Nop;

    // 统一操作数字段，按instruction_kind解释
    // How（示例）:
    // - Add: destination = first + second
    // - CmpEq: destination = (first == second)
    // - Branch: first_operand=条件，second_operand=真分支块，third_operand=假分支块
    IRValue destination_value = IRValue::makeInvalid();
    IRValue first_operand = IRValue::makeInvalid();
    IRValue second_operand = IRValue::makeInvalid();
    IRValue third_operand = IRValue::makeInvalid();

    // 可选附加字段，如call参数数量，源位置信息
    // Why:
    // - auxiliary_data 作为轻量扩展位，避免为少数指令膨胀结构体。
    // - source_line/source_column 让 IR 层错误可回溯到源码，便于诊断。
    uint32_t auxiliary_data = 0;
    uint32_t source_line = 0;
    uint32_t source_column = 0;
};

//---基本块---
// Why:
// - 显式基本块是控制流结构化的最小单位，为 jump/branch/phi 提供锚点。
// How:
// - instruction_list 顺序执行，末尾应由 terminator 指令结束（verify 约束）。
// - terminator 通常是 Jump / Branch / Return 之一。
struct IRBasicBlock {
    IRBlockId block_identifier = std::numeric_limits<IRBlockId>::max();
    std::string debug_block_name;
    std::vector<IRInst> instruction_list;
};

//---函数签名---
// Why:
// - 把签名独立出来，便于 call 校验、链接期检查和未来 ABI 扩展。
// How:
// - parameter_types + return_type 是 call 指令与符号检查的类型依据。
struct IRFunctionSignature {
    std::vector<IRType> parameter_types;
    IRType return_type = IRType::makeUnknown();
};

//---函数---
struct IRFunction {
    // Why:
    // - function_identifier 是模块内稳定身份；function_name_identifier 是用户可见名映射键。
    // - 二者分离可避免重命名、导出别名等场景下的身份歧义。
    IRFunctionId function_identifier = std::numeric_limits<IRFunctionId>::max();
    // 与现有字符串池对齐：函数名存name_id,避免跨模块字符串比较开销
    uint32_t function_name_identifier = std::numeric_limits<uint32_t>::max();

    // Why:
    // - source_path 直接挂在函数上，便于多文件项目的精确报错与审计。
    std::string function_source_path;
    IRFunctionSignature function_signature;

    // 形参对应的vreg列表(与function_signature.parameter_types 同长度)
    std::vector<IRRegId> parameter_registers;

    // 基本块
    IRBlockId entry_block_identifier = std::numeric_limits<IRBlockId>::max();
    std::vector<IRBasicBlock> basic_blocks;

    // virtual register 分配上界[0,next_virtual_register_identifier)
    IRRegId next_virtual_register_identifier = 0;

    // Why:
    // - IR 使用虚拟寄存器表达值流，避免在 builder 阶段绑定物理寄存器策略。
    // - 物理分配推迟到 lowering，可按目标 VM 约束选择策略。
    // How:
    // - 每次 allocateVirtualRegister() 返回一个新编号，形成 SSA-like 的值流基础。
    IRRegId allocateVirtualRegister() { return next_virtual_register_identifier++; }

    // Why:
    // - 构建基本块时自动分配 block_identifier，避免调用侧手动维护一致性。
    // How:
    // - block_identifier 采用当前 basic_blocks.size()，保持连续且可预测。
    IRBasicBlock &createBasicBlock(const std::string &block_name) {
        IRBasicBlock basic_block;
        basic_block.block_identifier = static_cast<IRBlockId>(basic_blocks.size());
        basic_block.debug_block_name = block_name;
        basic_blocks.push_back(basic_block);
        return basic_blocks.back();
    }
};

//---符号---
// Why:
// - 符号种类描述链接语义，而不是语法形态。
// - 例如 Function/Struct/GlobalVar 可指导链接冲突检查与入口决议。
// How:
// - 外部符号（External）可在链接阶段解析到其他模块定义。
enum class IRSymbolKind : uint8_t {
    Function = 0,
    Struct,
    GlobalVar,
    External
};

struct IRSymbol {
    // Why:
    // - symbol_identifier 是模块内符号主键。
    // - symbol_name_identifier 是和字符串池对齐的名称键，便于快速比较。
    IRSymbolId symbol_identifier = std::numeric_limits<IRSymbolId>::max();
    uint32_t symbol_name_identifier = std::numeric_limits<uint32_t>::max();
    IRSymbolKind symbol_kind = IRSymbolKind::External;
    IRType symbol_type = IRType::makeUnknown();

    // 该符号归属函数(函数符号时可用)，否则保持uint32_max
    IRFunctionId owner_function_identifier = std::numeric_limits<IRFunctionId>::max();

    // Why:
    // - owner_module_path 用于跨模块诊断（重复符号/可见性冲突时需要归属信息）。
    // - is_exported 明确该符号是否对外可见，避免 linker 通过启发式推断。
    // How:
    // - Linker 可据此执行：重名冲突检查、导出可见性过滤、入口决议。
    std::string owner_module_path;
    bool is_exported = false;
};

//---模块级IR---
struct ModuleIR {
    // Why:
    // - ModuleIR 是“单模块后端契约对象”，连接 builder/verify/lowering/linker。
    // - 它不重复 AST 语法细节，只保留后端需要的结构化信息。
    //
    // 关键边界：
    // - AST：语法层（解析友好、语法种类丰富）
    // - IR：后端层（执行友好、语义原语稳定）
    // - 因此“新增一个语法节点”通常修改 builder，而不是修改 ModuleIR 核心结构。
    std::string module_name;
    std::string module_source_path;

    // 与Chunk对齐：每个模块携带一份string_pool快照
    std::vector<std::string> module_string_pool;

    // 顶层初始化入口(模块加载时执行)
    IRFunctionId module_initializer_function_identifier = std::numeric_limits<IRFunctionId>::max();

    // 函数与符号表
    std::vector<IRFunction> function_table;
    std::vector<IRSymbol> symbol_table;

    // function_name_identifier -> symbol_identifier
    std::unordered_map<uint32_t, IRSymbolId> symbol_identifier_by_name_identifier;

    // Why:
    // - 统一入口创建函数，保证 function_identifier 与 function_table 下标一致。
    // How:
    // - 创建时写入 function_source_path，避免后续调用方忘记填来源路径。
    IRFunction &createFunction(uint32_t function_name_identifier, const std::string &source_path) {
        IRFunction function;
        function.function_identifier = static_cast<IRFunctionId>(function_table.size());
        function.function_name_identifier = function_name_identifier;
        function.function_source_path = source_path;
        function_table.push_back(function);
        return function_table.back();
    };

    // Why:
    // - addSymbol 实现“按名称去重”的最小语义，避免模块内重复插入。
    // - 冲突是否报错交给 verify/linker 决策，这里只提供一致的数据写入入口。
    // How:
    // - 先查 symbol_identifier_by_name_identifier，存在则复用，不存在再创建。
    // - 这样可以把“写入”与“规则判断”解耦，便于分阶段演进。
    IRSymbolId addSymbol(uint32_t symbol_name_identifier, IRSymbolKind symbol_kind, const IRType &symbol_type,
                         const std::string &owner_module_path, bool is_exported) {
        auto existing_symbol = symbol_identifier_by_name_identifier.find(symbol_name_identifier);
        if (existing_symbol != symbol_identifier_by_name_identifier.end()) {
            return existing_symbol->second;
        }

        IRSymbol symbol;
        symbol.symbol_identifier = static_cast<IRSymbolId>(symbol_table.size());
        symbol.symbol_name_identifier = symbol_name_identifier;
        symbol.symbol_kind = symbol_kind;
        symbol.symbol_type = symbol_type;
        symbol.owner_module_path = owner_module_path;
        symbol.is_exported = is_exported;

        symbol_table.push_back(symbol);
        symbol_identifier_by_name_identifier.emplace(symbol_name_identifier, symbol.symbol_identifier);
        return symbol.symbol_identifier;
    };

    // Why:
    // - 明确模块是否存在初始化函数，避免各处直接比较哨兵值。
    bool hasInitializerFunction() const {
        return module_initializer_function_identifier != std::numeric_limits<IRFunctionId>::max();
    }
};

} // namespace niki::ir