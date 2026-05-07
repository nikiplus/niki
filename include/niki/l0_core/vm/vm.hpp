#pragma once
/* vm.hpp —— 寄存器式字节码解释器接口。
 *
 * 这一层连接“静态编译产物”和“动态执行状态”：
 * - 编译阶段将语义正确的程序降级为 Chunk + 对象常量；
 * - VM 在运行期维护 CallFrame 栈，按 ip 顺序读取 opcode；
 * - 每个函数调用通过 base_register 映射到统一物理栈窗口，避免参数复制；
 * - 全局函数与结构体蓝图通过 name_id 建索引，供链接后的 launcher 解析入口。
 *
 * 从算法层面看，主循环是 Fetch-Decode-Execute；
 * 从工程层面看，这里是错误隔离边界：越界读、坏跳转、类型不匹配都统一上报为 RUNTIME_ERROR。
 */
#include "niki/l0_core/semantic/module_id.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <unordered_map>
#include <vector>

namespace niki::vm {

/// VM globals 复合键: (module_id, name_id)
struct GlobalKey {
    ModuleId module_id;
    uint32_t name_id;
    bool operator==(const GlobalKey &o) const { return module_id == o.module_id && name_id == o.name_id; }
};
struct GlobalKeyHash {
    size_t operator()(const GlobalKey &k) const {
        return std::hash<uint64_t>{}((static_cast<uint64_t>(k.module_id) << 32) ^ k.name_id);
    }
};

/// 一次激活调用：一段寄存器窗口 + 一份字节码。
struct CallFrame {
    ObjFunction *function; ///< 当前函数（内含 chunk、arity、max_registers 等）
    uint8_t *ip;           ///< 下一条指令在 `chunk.code` 中的位置
    size_t base_register;  ///< 逻辑寄存器 0 对应 `stack[base_register]`
    /**
     * out_register:
     * - Why: 返回值需要回写到“调用者窗口”中的一个槽位。
     * - How: OP_CALL 入栈时记录 caller 期望接收结果的寄存器号，OP_RETURN 弹栈后据此回填。
     * - 这让“被调函数固定写 r0”与“调用者自选接收寄存器”能够同时成立。
     */
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
    /// 可理解为“解释器级寄存器文件”的硬上限，不等同于调用深度上限。
    static constexpr size_t stack_capacity = 8192;

    VM() = default;

    /**
     * @brief 执行裸 Chunk（脚本顶层入口）。
     * @param chunk 字节码块（含常量池、字符串池、寄存器槽位上界）。
     * @param should_print 是否在顶层 RETURN 时打印结果。
     * @return 成功返回 Value，失败返回运行时错误码。
     */
    std::expected<Value, InterpretResult> executeChunk(const Chunk &chunk, bool should_print);
    /**
     * @brief 执行函数对象入口。
     * @param function 函数对象指针。
     * @param should_print 是否在顶层 RETURN 时打印结果。
     * @return 成功返回 Value，失败返回运行时错误码。
     */
    std::expected<Value, InterpretResult> executeFunction(ObjFunction *function, bool should_print);

    /**
     * @brief 按名字查询全局函数（MVP 线性扫描字符串池）。
     * @param name 函数字面量名。
     * @return 找到返回函数指针，否则返回 nullptr。
     */
    ObjFunction *lookupGlobalFunctionByName(const std::string &name);
    /**
     * @brief 按 (module_id, name_id) 查询全局函数表。
     * @param module_id 模块 id。
     * @param name_id 字符串池 id。
     * @return 找到返回函数指针，否则返回 nullptr。
     */
    ObjFunction *lookupGlobalFunctionById(ModuleId module_id, uint32_t name_id);

  private:
    std::array<Value, stack_capacity> stack{}; ///< 全局寄存器文件
    std::vector<CallFrame> frames;             ///< 调用栈；`OP_CALL` / `OP_RETURN` 维护

    std::unordered_map<GlobalKey, ObjFunction *, GlobalKeyHash>
        globals; ///< 顶层函数：(module_id, name_id) -> ObjFunction*
    std::unordered_map<GlobalKey, Object *, GlobalKeyHash>
        global_objects; ///< 全局对象：(module_id, name_id) -> Object*

    const std::vector<std::string> *current_string_pool = nullptr; ///< 当前上下文字符串池（诊断、按名查找）
    ModuleId current_chunk_module_id_ = kInvalidModuleId;          ///< 当前执行 chunk 所属模块 id

    CallFrame *currentFrame = nullptr; ///< 等价于 `frames` 非空时指向最后一帧

    /** @brief 取指-译码-执行主循环。 */
    std::expected<Value, InterpretResult> run(bool should_print = false);

    /// 当前帧逻辑寄存器 `r` 即 `stack[base_register + r]`。
    /// 这是“寄存器窗口”抽象的核心：逻辑寄存器编号通过 base 偏移映射到统一物理数组。
    Value *currentRegisters() { return &stack[currentFrame->base_register]; }

    /**
     * @brief 读取当前帧常量池项。
     * @param index 常量索引。
     * @return 常量值；若越界则返回 RUNTIME_ERROR。
     */
    std::expected<Value, InterpretResult> readConstantByIndex(uint32_t index);
    /** @brief 从当前 ip 读取 1 字节并前进。 @param byte_out 输出字节。 */
    bool tryReadByte(uint8_t *byte_out);
    /** @brief 从当前 ip 读取大端 16 位立即数并前进。 @param short_out 输出立即数。 */
    bool tryReadShort(uint16_t *short_out);
    /** @brief 从当前 ip 向前相对跳转。 @param offset 偏移字节数。 */
    bool tryJumpForward(uint16_t offset);
    /** @brief 从当前 ip 向后回跳。 @param offset 偏移字节数。 */
    bool tryJumpBackward(uint16_t offset);
    /** @brief 校验二元整数操作数并解包。 */
    bool requireInt64(Value left, Value right, int64_t *left_integer, int64_t *right_integer);
    /** @brief 校验二元浮点操作数并解包。 */
    bool requireFloat64(Value left, Value right, double *left_float, double *right_float);
    /** @brief 校验单个浮点操作数并解包。 */
    bool requireFloat64(Value value, double *resolved_float);

    /** @brief 打印运行时错误与调用栈。 @param format printf 风格格式串。 */
    void runtime_error(const char *format, ...);
};
} // namespace niki::vm
