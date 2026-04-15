#pragma once
#include <cstdint>

namespace niki::vm {

// 运行时的基础类型标签（只关注内存怎么读，不关注具体是哪个 Struct）
// 这里的类型标签是给虚拟机看的，是用来进行计算的，追求内存对齐和读写速度
// 是给虚拟机看的，不是给程序员看的，千万不要和NKBaseType混淆
enum class ValueType : uint8_t {
    Nil,
    Bool,
    Integer,
    Float,
    Object // 所有堆上分配的复杂类型（String, Struct, Array）的统一入口
};

// DOD 架构的核心：紧凑的运行时操作数栈元素 (Tagged Union, 16 字节)
struct Value {
    ValueType type;

    union {
        bool boolean;
        int64_t integer;
        double floating;
        void *object; // 指向堆上的实际数据
    } as;

    // --- 快速构造工厂 ---
    static Value makeNil() { return {ValueType::Nil, {.integer = 0}}; }
    static Value makeBool(bool b) { return {ValueType::Bool, {.boolean = b}}; }
    static Value makeInt(int64_t i) { return {ValueType::Integer, {.integer = i}}; }
    static Value makeFloat(double f) { return {ValueType::Float, {.floating = f}}; }
    static Value makeObject(void *obj) { return {ValueType::Object, {.object = obj}}; }
};

} // namespace niki::vm