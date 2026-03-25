#pragma once
#include <cassert>
#include <cstdint>

namespace niki::syntax {

// 编译期的核心数据类型（不包含具体的值）
// 这里的类型标签是给编译器看的，是用来防错和类型检查的，包含了许多逻辑信息
// 也就是给程序员看的，哄小宝宝用的，千万不要和value混淆
enum class NKBaseType : uint8_t {
    Void,    // 空类型（如函数无返回值）
    Bool,    // 布尔型
    Integer, // 整数
    Float,   // 浮点数
    String,  // 字符串
    // 复杂类型
    Array,  // 数组
    Map,    // 映射
    Object, // 结构体/组件实例
    Entity, // ECS实体句柄
    Unknown // 编译期推导过程中的占位符
};

// 完整的静态类型签名 (极致优化的 4 字节句柄)
// 物理内存布局: [8位 BaseType] [24位 TypeID]
struct NKType {
    uint32_t handle;

    // --- 构造函数 ---
    explicit NKType(uint32_t h) : handle(h) {}
    NKType(NKBaseType base, int32_t type_id) {
        // 确保 type_id 没溢出 24 位（考虑到符号位）
        assert(type_id >= -1 && type_id <= 0x7FFFFF);
        // 将 base 移到最高 8 位，type_id 放在低 24 位 (屏蔽符号位干扰)
        // 位运算乍一看有点吓人，事实上很简单，事实上多少位位运算，就等于我们在用一个多大的盒子。
        // 这里的uint32_t就是在说，我们用一个有32个格子的盒子，用它来装东西。
        /*所以我们总共相当于声明了两个盒子
        |00000111 00000000 00000000 00000000  (BaseType 部分)basetype只有8位，因为我们上面定义了它是uint8_t
        |00000000 00000000 00000000 01100100  (TypeID 部分)
        -------------------------------------使用位运算符“|”，将base和type_id合并起来
        |00000111 00000000 00000000 01100100  (最终的 handle)
        */
        handle = (static_cast<uint32_t>(base) << 24) | (static_cast<uint32_t>(type_id) & 0x00FFFFFF);
    }

    // --- 数据提取 ---
    /*还是在脑子里想象那个盒子，现在我们要把盒子里的东西取出来
    *我们先把base取出来，它是最高的8位，所以我们右移24位，然后把结果强制转换为NKBaseType(左高右低)
    *↓______↓ 也就是这一部分
    |00000111 00000000 00000000 01100100 (最终的 handle)
    那么最终得到的就是00000111，也就是NKBaseType::Integer
    */
    NKBaseType getBase() const { return static_cast<NKBaseType>(handle >> 24); }

    /*我们再把type_id取出来，它是最低的24位，所以我们直接取出来，然后把结果强制转换为int32_t(左高右低)
    *为了防止计算出错，我们使用0x00FFFFFF做按位与操作，把高8位都置为0，只保留低24位。
    *         ↓________________________↓也就是这一部分
    |00000000 00000000 00000000 01100100 (被我们使用0x00FFFFFF做按位与操作消除了高八位的数据！)
    那么最终得到的就是01100100，也就是100
    */
    int32_t getTypeId() const {
        /*我们先来解释0x00FFFFFF
        *0x00FFFFFF是一个32位的整数，它的二进制表示为00000000 11111111 11111111 11111111
        *首先我们来理解位运算里的“|”和“&”
        *我们可以将“|”理解为“加法”，“&”理解为“乘法”。
        *我们把0x00FFFFFF和handle同时列出
        |00000000 00000000 00000000 01100100 (handle)
        &00000000 11111111 11111111 11111111 (0x00FFFFFF)
        -------------------------------------使用位运算符“&”，上下相乘，只有与1相乘的位留了下来！这也是“位掩码(mask)”的操作方法。
        |00000000 00000000 00000000 01100100 (最终的结果)
        */
        uint32_t id = handle & 0x00FFFFFF;
        // 如果type_id是0x00FFFFFF，那么说明它是Unknown类型，我们返回-1
        // “这是因为我们在构造时通过 & 0x00FFFFFF 把 -1（全 1）截断成了 0x00FFFFFF。现在手动判断并还原它
        // 是为了防止 static_cast 将其误认为正整数 16,777,215。”
        if (id == 0x00FFFFFF)
            return -1;
        // 否则，我们直接把type_id强制转换为int32_t返回，这个值就是我们之前构造时传入的type_id
        return static_cast<int32_t>(id);
    }

    // --- 快速构造工厂 ---
    static NKType makeUnknown() { return NKType(NKBaseType::Unknown, -1); }
    static NKType makeInt() { return NKType(NKBaseType::Integer, -1); }
    static NKType makeFloat() { return NKType(NKBaseType::Float, -1); }
    static NKType makeBool() { return NKType(NKBaseType::Bool, -1); }
    static NKType makeObject(int32_t struct_id) { return NKType(NKBaseType::Object, struct_id); }

    // 极速的类型比对：只需一条 32 位整数比较指令
    bool operator==(const NKType &other) const { return handle == other.handle; }
    bool operator!=(const NKType &other) const { return handle != other.handle; }
};

} // namespace niki::syntax