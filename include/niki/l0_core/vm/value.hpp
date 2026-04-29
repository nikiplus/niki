#pragma once
#include <cstdint>

namespace niki::vm {

/*
 * value.hpp —— VM 运行时值模型（Tagged Union）。
 *
 * 该文件定义解释器在寄存器窗口和常量池里流动的最小数据单元 `Value`。
 * 从计算机体系结构视角看，解释器执行本质是“读取字节码 -> 访问寄存器槽 -> 做算术/控制流”；
 * 因而 Value 的设计目标是：
 * 1) 常见标量类型可在固定大小槽位内原地存取，避免频繁堆分配；
 * 2) 复杂对象通过统一对象指针进入对象系统，保持调度路径一致；
 * 3) 类型标签与数据载荷紧耦合，任何算术/比较前都可做快速守卫检查。
 */

/// 运行时基础类型标签（面向 VM 执行，不等同于语言层声明类型系统）。
enum class ValueType : uint8_t {
    Nil,     ///< 空值。
    Bool,    ///< 布尔值。
    Integer, ///< 64 位有符号整数。
    Float,   ///< 64 位浮点数。
    Object   ///< 指向堆对象（String/Array/Map/Function/StructDef/Instance）。
};

/// VM 的寄存器槽元素：紧凑 tagged-union。
struct Value {
    ValueType type; ///< 标签，决定 union 哪个成员有效。

    union {
        bool boolean;   ///< `ValueType::Bool` 载荷。
        int64_t integer; ///< `ValueType::Integer` 载荷。
        double floating; ///< `ValueType::Float` 载荷。
        void *object;    ///< `ValueType::Object` 载荷，指向对象头。
    } as;               ///< 原始数据载荷。

    /** @brief 构造 nil 值。 */
    static Value makeNil() { return {ValueType::Nil, {.integer = 0}}; }
    /** @brief 构造布尔值。 @param b 布尔载荷。 */
    static Value makeBool(bool b) { return {ValueType::Bool, {.boolean = b}}; }
    /** @brief 构造整数值。 @param i 整数载荷。 */
    static Value makeInt(int64_t i) { return {ValueType::Integer, {.integer = i}}; }
    /** @brief 构造浮点值。 @param f 浮点载荷。 */
    static Value makeFloat(double f) { return {ValueType::Float, {.floating = f}}; }
    /** @brief 构造对象值。 @param object_ptr 指向对象头的非托管指针。 */
    static Value makeObject(void *object_ptr) { return {ValueType::Object, {.object = object_ptr}}; }
};

} // namespace niki::vm