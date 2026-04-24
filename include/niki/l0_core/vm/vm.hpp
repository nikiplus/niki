#pragma once
/* niki::vm::VM —— 寄存器式字节码解释器（l0 运行时核心）。
 *
 * 模型概要：
 * - 连续物理栈 `stack`（容量 `stack_capacity`）；每帧 `base_register` 指定本帧逻辑寄存器 0 在栈上的起点。
 * - `CallFrame` 持有当前 `ObjFunction::chunk` 与 `ip`；`OP_CALL` 时新帧 `base_register` 相对实参槽滑动。
 * - `OP_RETURN` 将寄存器 0 写回调用帧的 `out_register` 并弹栈。
 * - `globals` / `global_objects` 存放顶层函数与结构体蓝图等，供链接后与 launcher 解析入口。
 */
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <unordered_map>
#include <vector>

namespace niki::vm {

/// 一次激活调用：一段寄存器窗口 + 一份字节码。
struct CallFrame {
    ObjFunction *function; ///< 当前函数（内含 chunk、arity、max_registers 等）
    uint8_t *ip;          ///< 下一条指令在 `chunk.code` 中的位置
    size_t base_register; ///< 逻辑寄存器 0 对应 `stack[base_register]`
    uint8_t out_register; ///< `OP_RETURN` 时结果写回调用者的逻辑寄存器下标
};

/// 与 `std::expected` 错误通道配合的执行结果枚举。
enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

class VM {
  public:
    /// 物理 `Value` 槽位数；与编译器写入的 `max_registers` 及 `OP_CALL` 边界检查一致。
    static constexpr size_t stack_capacity = 8192;

    VM() = default;

    /// 用临时 `ObjFunction` 包装裸 `Chunk` 执行（脚本顶层）；不解析工程入口名。
    std::expected<Value, InterpretResult> executeChunk(const Chunk &chunk, bool should_print);
    /// 从已有 `ObjFunction` 入口执行（新栈、base=0）。
    std::expected<Value, InterpretResult> executeFunction(ObjFunction *function, bool should_print);

    /// 按字面量函数名在 `current_string_pool` 中线性查找（MVP）。
    ObjFunction *lookupGlobalFunctionByName(const std::string &name);
    /// 按 name_id 查 `globals`。
    ObjFunction *lookupGlobalFunctionById(uint32_t id);

  private:
    std::array<Value, stack_capacity> stack{}; ///< 全局寄存器文件
    std::vector<CallFrame> frames;              ///< 调用栈；`OP_CALL` / `OP_RETURN` 维护

    std::unordered_map<uint32_t, ObjFunction *> globals;   ///< 顶层函数：name_id -> ObjFunction*
    std::unordered_map<uint32_t, Object *> global_objects; ///< 全局对象：如 StructDef

    const std::vector<std::string> *current_string_pool = nullptr; ///< 当前上下文字符串池（诊断、按名查找）

    CallFrame *currentFrame = nullptr; ///< 等价于 `frames` 非空时指向最后一帧

    /// 取指—译码—执行主循环。
    std::expected<Value, InterpretResult> run(bool should_print = false);

    /// 当前帧逻辑寄存器 `r` 即 `stack[base_register + r]`。
    Value *currentRegisters() { return &stack[currentFrame->base_register]; }

    /// 当前帧常量池索引访问；越界返回 `RUNTIME_ERROR`。
    std::expected<Value, InterpretResult> readConstantByIndex(uint32_t index);
    /// 从 `ip` 读取一字节并前进；防止 `code` 截断导致越界读。
    bool tryReadByte(uint8_t *byte_out);
    /// 大端 16 位立即数；与编译器 `patchJump` 等布局一致。
    bool tryReadShort(uint16_t *short_out);
    /// 相对当前 `ip` 向前跳转，目标须在 `chunk.code` 范围内。
    bool tryJumpForward(uint16_t offset);
    /// 相对当前 `ip` 向后回跳（循环头），目标不得早于 code 起点。
    bool tryJumpBackward(uint16_t offset);
    /// 校验两侧均为整数并解包到输出参数（不信任 bytecode 类型标签时调用）。
    bool requireInt64(Value left, Value right, int64_t *left_integer, int64_t *right_integer);
    /// 校验两侧均为浮点。
    bool requireFloat64(Value left, Value right, double *left_float, double *right_float);
    /// 单操作数浮点校验（如取负）。
    bool requireFloat64(Value value, double *resolved_float);

    /// 格式化错误信息并打印简化栈回溯（stderr）。
    void runtime_error(const char *format, ...);
};
} // namespace niki::vm
