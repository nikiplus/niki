#pragma once
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace niki::vm {
/*
 * object.hpp —— VM 堆对象系统与内存布局。
 *
 * Value 层只保存“对象指针”，真正复杂数据都在这里定义。
 * 本文件采用“统一对象头 + 具体负载结构”的 C 风格多态布局：
 * - 统一对象头保证运行时可先判别 ObjType，再进行安全下转；
 * - 对于定长/近定长数据（String、Instance）使用柔性数组保持紧凑；
 * - 对于需要扩容且地址稳定要求高的数据（Array、Map）采用头与数据块分离。
 *
 * 该设计平衡了可读性与执行效率，并与 OP_NEW_*| GET_* | SET_*指令一一对应。
 */

// 堆对象的类型标签
enum class ObjType : uint8_t {
    String,
    Array,
    Map,
    StructDef,
    Instance,
    Function,
};

// 基础对象头 (所有堆对象的公共前缀)
// 必须放在所有具体对象结构体的最开头，以保证指针强转 (C-style polymorphism) 的绝对安全
struct Object {
    ObjType type;  ///< 对象运行时类型标签。
    bool isMarked; ///< 预留 GC 标记位（当前 MVP 尚未启用完整 GC）。
};
// 结构体蓝图(元数据)：存活在VM全局表中，描述了一个自定义类型的物理形状
struct ObjStructDef {
    Object object_header; ///< 统一对象头。
    uint32_t name_id;     ///< 结构体名在字符串池中的 id。
    uint32_t field_count; ///< 字段数（实例 fields 的固定长度）。
};
// 结构体实例：真正的物理数据实体，分配在堆上
struct ObjInstance {
    Object object_header; ///< 统一对象头。
    ObjStructDef *struct_definition; ///< 指向类型蓝图。
    Value fields[]; ///< 柔性数组字段区，长度由 struct_definition->field_count 决定。
};

// DOD 风格的字符串对象
// 拒绝内部包含 std::string (那会导致二次堆分配和缓存不连续)
struct ObjString {
    Object object_header; ///< 统一对象头。
    uint32_t length;      ///< 字符串字节长度（不含 '\0'）。
    char chars[];         ///< 柔性数组字符区，尾部额外补 '\0' 便于调试输出。
};

// DOD 风格的数组对象
// 拒绝使用 std::vector<Value> (同样会导致二次堆分配)
// 因为数组需要动态扩容 (push)，如果我们使用柔性数组 (elements[])，
// 当我们调用 realloc 扩容时，整个 ObjArray 结构体的物理首地址可能会改变！
// 这会导致 VM 寄存器中原本指向这个 ObjArray 的所有 Value 指针瞬间变成悬空指针 (Dangling Pointer)。
// 因此，对于需要动态改变大小的对象，我们必须采用“对象头与数据块分离”的设计，或者禁止重新分配地址。
struct ObjArray {
    Object object_header; ///< 统一对象头。
    uint32_t capacity;    ///< 元素块容量。
    uint32_t count;       ///< 当前元素数量。
    Value *elements;      ///< 指向独立分配的连续元素块。
};

struct ObjMapEntry {
    Value key;     ///< 键。
    Value value;   ///< 值。
    bool occupied; ///< 是否已占用。
};

struct ObjMap {
    Object object_header; ///< 统一对象头。
    uint32_t capacity;    ///< entry 槽位数。
    uint32_t count;       ///< 已占用槽位数。
    ObjMapEntry *entries; ///< entry 连续数组。
};

// 函数对象：包含字节码、常量池、行号信息以及函数的元数据
// (目前我们暂时保持其 C++ 类的形态，未来如果有需要也可以拍扁成 DOD 结构)
struct ObjFunction {
    Object object_header; ///< 补齐对象头，使其参与对象系统多态。
    uint32_t name_id;     ///< 函数名字符串 id。
    uint8_t arity;        ///< 形参数量。
    /// 本函数体使用的寄存器槽位上界（比最大寄存器下标多 1）；供 VM 与 OP_CALL 栈安全检查。
    uint16_t max_registers;
    niki::Chunk chunk; // 属于这个函数的独立字节码块
};

//--- DOD内存分配器---

/** @brief 分配结构体蓝图对象。 @param name_id 名字 id。 @param field_count 字段数。 */
inline ObjStructDef *allocateStructDef(uint32_t name_id, uint32_t field_count) {
    ObjStructDef *struct_definition = static_cast<ObjStructDef *>(std::malloc(sizeof(ObjStructDef)));
    struct_definition->object_header.type = ObjType::StructDef;
    struct_definition->object_header.isMarked = false;
    struct_definition->name_id = name_id;
    struct_definition->field_count = field_count;
    return struct_definition;
}
/** @brief 按蓝图分配结构体实例并将字段初始化为 nil。 @param struct_definition 类型蓝图。 */
inline ObjInstance *allocateInstance(ObjStructDef *struct_definition) {
    size_t allocation_size = sizeof(ObjInstance) + struct_definition->field_count * sizeof(Value);
    ObjInstance *instance = static_cast<ObjInstance *>(std::malloc(allocation_size));
    instance->object_header.type = ObjType::Instance;
    instance->object_header.isMarked = false;
    instance->struct_definition = struct_definition;

    for (uint32_t field_index = 0; field_index < struct_definition->field_count; ++field_index) {
        instance->fields[field_index] = Value::makeNil();
    }
    return instance;
}

/** @brief 分配字符串对象（头+字符+终止符一次性连续分配）。 @param chars 字符串字节序列。 @param length 字节长度。 */
inline ObjString *allocateString(const char *chars, uint32_t length) {
    // 分配结构体大小，加上字符串，再加一个字节给\0
    ObjString *string_object = static_cast<ObjString *>(std::malloc(sizeof(ObjString) + length + 1));
    string_object->object_header.type = ObjType::String;
    string_object->object_header.isMarked = false;
    string_object->length = length;
    std::memcpy(string_object->chars, chars, length);
    string_object->chars[length] = '\0';
    return string_object;
}

/** @brief 分配数组对象头与元素块。 @param capacity 初始容量。 */
inline ObjArray *allocateArray(uint32_t capacity) {
    // 数组对象本身是一个固定大小的块
    ObjArray *array = static_cast<ObjArray *>(std::malloc(sizeof(ObjArray)));
    array->object_header.type = ObjType::Array;
    array->object_header.isMarked = false;
    array->capacity = capacity;
    array->count = 0;
    // 数据块在另一块绝对连续的堆内存中
    if (capacity > 0) {
        array->elements = static_cast<Value *>(std::malloc(capacity * sizeof(Value)));
        // 默认全部初始化位 Nil，防止读到内存垃圾
        for (uint32_t element_index = 0; element_index < capacity; ++element_index) {
            array->elements[element_index] = Value::makeNil();
        }
    } else {
        array->elements = nullptr;
    }
    return array;
}
/** @brief 扩容数组元素块并将新增槽位初始化为 nil。 @param array 目标数组。 @param new_capacity 新容量。 */
inline void expandArray(ObjArray *array, uint32_t new_capacity) {
    if (new_capacity <= array->capacity) {
        return;
    }
    // realloc 会自动把旧数据拷贝到新内存块（如果发生了地址迁移）
    // 且我们只改变array->elements的指向，array自身的地址不改变
    array->elements = static_cast<Value *>(std::realloc(array->elements, new_capacity * sizeof(Value)));

    // 初始化开辟新内存
    for (uint32_t element_index = array->capacity; element_index < new_capacity; ++element_index) {
        array->elements[element_index] = Value::makeNil();
    }
    array->capacity = new_capacity;
}

/** @brief 分配哈希映射对象并初始化 entry 表。 @param capacity 初始容量。 */
inline ObjMap *allocateMap(uint32_t capacity) {
    // 线性探测至少需要一个小容量，避免 0 容量导致每次 set 都要扩容
    if (capacity < 8) {
        capacity = 8;
    }
    ObjMap *map = static_cast<ObjMap *>(std::malloc(sizeof(ObjMap)));
    map->object_header.type = ObjType::Map;
    map->object_header.isMarked = false;
    map->capacity = capacity;
    map->count = 0;
    map->entries = static_cast<ObjMapEntry *>(std::malloc(sizeof(ObjMapEntry) * capacity));
    for (uint32_t entry_index = 0; entry_index < capacity; ++entry_index) {
        map->entries[entry_index].occupied = false;
        map->entries[entry_index].key = Value::makeNil();
        map->entries[entry_index].value = Value::makeNil();
    }
    return map;
}

/** @brief 比较两个 Value 是否表示同一个键语义。 @param lhs 左值。 @param rhs 右值。 */
inline bool valueKeyEquals(Value lhs, Value rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    switch (lhs.type) {
    case ValueType::Nil:
        return true;
    case ValueType::Bool:
        return lhs.as.boolean == rhs.as.boolean;
    case ValueType::Integer:
        return lhs.as.integer == rhs.as.integer;
    case ValueType::Float:
        return lhs.as.floating == rhs.as.floating;
    case ValueType::Object: {
        auto *left_object = static_cast<Object *>(lhs.as.object);
        auto *right_object = static_cast<Object *>(rhs.as.object);
        if (left_object->type != right_object->type) {
            return false;
        }
        if (left_object->type == ObjType::String) {
            auto *left_string = static_cast<ObjString *>(lhs.as.object);
            auto *right_string = static_cast<ObjString *>(rhs.as.object);
            return left_string->length == right_string->length &&
                   std::memcmp(left_string->chars, right_string->chars, left_string->length) == 0;
        }
        // 其他对象类型退化为地址等价
        return lhs.as.object == rhs.as.object;
    }
    }
    return false;
}

/** @brief 扩容并重建 map entry 表。 @param map 目标 map。 @param new_capacity 新容量。 */
inline void expandMap(ObjMap *map, uint32_t new_capacity) {
    if (new_capacity <= map->capacity) {
        return;
    }
    ObjMapEntry *new_entries = static_cast<ObjMapEntry *>(std::malloc(sizeof(ObjMapEntry) * new_capacity));
    for (uint32_t new_entry_index = 0; new_entry_index < new_capacity; ++new_entry_index) {
        new_entries[new_entry_index].occupied = false;
        new_entries[new_entry_index].key = Value::makeNil();
        new_entries[new_entry_index].value = Value::makeNil();
    }
    for (uint32_t old_entry_index = 0; old_entry_index < map->capacity; ++old_entry_index) {
        if (!map->entries[old_entry_index].occupied) {
            continue;
        }
        // 线性探测插入到新表
        for (uint32_t candidate_index = 0; candidate_index < new_capacity; ++candidate_index) {
            if (!new_entries[candidate_index].occupied) {
                new_entries[candidate_index].occupied = true;
                new_entries[candidate_index].key = map->entries[old_entry_index].key;
                new_entries[candidate_index].value = map->entries[old_entry_index].value;
                break;
            }
        }
    }
    std::free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

/** @brief 写入 map 键值（已存在则覆盖，否则插入）。 @param map 目标 map。 @param key 键。 @param value 值。 */
inline void mapSet(ObjMap *map, Value key, Value value) {
    // 无删除场景，超过容量阈值后扩容
    if (map->count >= map->capacity) {
        expandMap(map, map->capacity < 8 ? 8 : map->capacity * 2);
    }
    // 先尝试覆盖已有 key
    for (uint32_t entry_index = 0; entry_index < map->capacity; ++entry_index) {
        if (map->entries[entry_index].occupied && valueKeyEquals(map->entries[entry_index].key, key)) {
            map->entries[entry_index].value = value;
            return;
        }
    }
    // 再写入空槽
    for (uint32_t entry_index = 0; entry_index < map->capacity; ++entry_index) {
        if (!map->entries[entry_index].occupied) {
            map->entries[entry_index].occupied = true;
            map->entries[entry_index].key = key;
            map->entries[entry_index].value = value;
            map->count++;
            return;
        }
    }
}

/** @brief 读取 map 键值。 @param map 目标 map。 @param key 键。 @param out_value 输出值。 @return 找到返回 true。 */
inline bool mapGet(ObjMap *map, Value key, Value *out_value) {
    for (uint32_t entry_index = 0; entry_index < map->capacity; ++entry_index) {
        if (!map->entries[entry_index].occupied) {
            continue;
        }
        if (valueKeyEquals(map->entries[entry_index].key, key)) {
            *out_value = map->entries[entry_index].value;
            return true;
        }
    }
    return false;
}
//---安全类型转换与识别---
/** @brief 判断 value 是否是指定 ObjType 的对象。 @param value 输入值。 @param type 目标对象类型。 */
inline bool isObjType(Value value, ObjType type) {
    return value.type == ValueType::Object && static_cast<Object *>(value.as.object)->type == type;
}
/** @brief 判断是否结构体蓝图对象。 */
inline bool isStructDef(Value value) { return isObjType(value, ObjType::StructDef); }
/** @brief 判断是否结构体实例对象。 */
inline bool isInstance(Value value) { return isObjType(value, ObjType::Instance); }
/** @brief 判断是否字符串对象。 */
inline bool isString(Value value) { return isObjType(value, ObjType::String); }
/** @brief 判断是否数组对象。 */
inline bool isArray(Value value) { return isObjType(value, ObjType::Array); }
/** @brief 判断是否映射对象。 */
inline bool isMap(Value value) { return isObjType(value, ObjType::Map); }
/** @brief 判断是否函数对象。 */
inline bool isFunction(Value value) { return isObjType(value, ObjType::Function); }

/** @brief 将 Value 转为 ObjString*（调用方需先做 isString 守卫）。 */
inline ObjString *asString(Value value) { return static_cast<ObjString *>(value.as.object); }
/** @brief 将 Value 转为 ObjArray*（调用方需先做 isArray 守卫）。 */
inline ObjArray *asArray(Value value) { return static_cast<ObjArray *>(value.as.object); }
/** @brief 将 Value 转为 ObjMap*（调用方需先做 isMap 守卫）。 */
inline ObjMap *asMap(Value value) { return static_cast<ObjMap *>(value.as.object); }
/** @brief 将 Value 转为 ObjFunction*（调用方需先做 isFunction 守卫）。 */
inline ObjFunction *asFunction(Value value) { return static_cast<ObjFunction *>(value.as.object); }
// 注意：由于我们目前没有实现 GC 和 OP_FREE，
// 调用 allocateString 和 allocateArray 产生的内存暂时会泄漏。
// 这是 MVP 阶段的架构妥协。
} // namespace niki::vm
