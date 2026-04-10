#pragma once
#include "niki/vm/chunk.hpp"
#include "niki/vm/value.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace niki::vm {

// 堆对象的类型标签
enum class ObjType : uint8_t {
    String,
    Array,
    // 未来可扩展 Map, Struct 等
};

// 基础对象头 (所有堆对象的公共前缀)
// 必须放在所有具体对象结构体的最开头，以保证指针强转 (C-style polymorphism) 的绝对安全
struct Object {
    ObjType type;
    bool isMarked; // 预留给未来内存池/生命周期扫描的标记位
};

// DOD 风格的字符串对象
// 拒绝内部包含 std::string (那会导致二次堆分配和缓存不连续)
struct ObjString {
    Object obj;      // 对象头
    uint32_t length; // 字符串长度
    char chars[];    // 柔性数组 (Flexible Array Member): 字符串内容紧贴在 length 之后！
};

// DOD 风格的数组对象
// 拒绝使用 std::vector<Value> (同样会导致二次堆分配)
struct ObjArray {
    Object obj;        // 对象头
    uint32_t capacity; // 物理容量
    uint32_t count;    // 实际元素个数
    Value elements[];  // 柔性数组: 元素数组紧贴在 count 之后！
};

// 函数对象：包含字节码、常量池、行号信息以及函数的元数据
// (目前我们暂时保持其 C++ 类的形态，未来如果有需要也可以拍扁成 DOD 结构)
struct ObjFunction {
    size_t arity = 0;  // 参数个数
    niki::Chunk chunk; // 属于这个函数的独立字节码块
    std::string name;  // 函数名 (顶层脚本可为空)
};
//--- DOD内存分配器---
// 分配字符串：分配头+字符+、0
inline ObjString *allocateString(const char *chars, uint32_t length) {
    // 分配结构体大小，加上字符串，再加一个字节给\0
    ObjString *string = static_cast<ObjString *>(std::malloc(sizeof(ObjString) + length + 1));
    string->obj.type = ObjType::String;
    string->obj.isMarked = false;
    string->length = length;
    std::memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

// 分配数组：一次性分配 头+N个value
inline ObjArray *allocateArray(uint32_t capacity) {
    // 分配结构体大小，加上capacity个value的大小(16字节*capacity)
    ObjArray *array = static_cast<ObjArray *>(std::malloc(sizeof(ObjArray) + capacity * sizeof(Value)));
    array->obj.type = ObjType::Array;
    array->obj.isMarked = false;
    array->capacity = capacity;
    array->count = 0;
    // 默认全部初始化位 Nil，防止读到内存垃圾
    for (uint32_t i = 0; i < capacity; ++i) {
        array->elements[i] = Value::makeNil();
    }
    return array;
}

//---安全类型转换与识别---
inline bool isObjType(Value value, ObjType type) {
    return value.type == ValueType::Object && static_cast<Object *>(value.as.object)->type == type;
}

inline bool isString(Value value) { return isObjType(value, ObjType::String); }
inline bool isArray(Value value) { return isObjType(value, ObjType::Array); }

inline ObjString *asString(Value value) { return static_cast<ObjString *>(value.as.object); }
inline ObjString *asArray(Value value) { return static_cast<ObjString *>(value.as.object); }
// 注意：由于我们目前没有实现 GC 和 OP_FREE，
// 调用 allocateString 和 allocateArray 产生的内存暂时会泄漏。
// 这是 MVP 阶段的架构妥协。
} // namespace niki::vm