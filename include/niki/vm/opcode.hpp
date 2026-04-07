#pragma once
#include <cstdint>

namespace niki::vm {
/** @brief 指令集
 * @enum OPCODE 指令枚举
 * @details 每个指令对应一个唯一的 opcode
 * @note 指令枚举的 opcode 从 0x00 开始，每个指令占用 1 字节
 * @note [USER] VM & Compiler
 */
enum class OPCODE : uint8_t {
    /*---[CalcExpr]运算(四则运算 + 比较运算符 + 逻辑运算符 + 位运算符)---*/
    _CALC_START, // 运算指令开始

    // [BinaryExpr]二元运算(四则运算 + 比较运算符)
    __BINARY_START, // 二元运算指令开始
    // [ArithExpr]四则运算（+ - * / %）
    ___ARITH_START, // 四则运算指令开始
    // 整数运算
    ____INT_ARITH_START, // 整数四则运算指令开始
    OP_IADD,             // int + int
    OP_ISUB,             // int - int
    OP_IMUL,             // int * int
    OP_IDIV,             // int / int
    OP_IMOD,             // int % int
    ____INT_ARITH_END,   // 整数四则运算指令结束
    // 浮点运算
    ____FLOAT_ARITH_START, // 浮点四则运算指令开始
    OP_FADD,               // float + float
    OP_FSUB,               // float - float
    OP_FMUL,               // float * float
    OP_FDIV,               // float / float
    ____FLOAT_ARITH_END,   // 浮点四则运算指令结束
    ___ARITH_END,          // 四则运算指令结束
    // [CmpExpr]比较运算符（== != < > <= >=）
    ___CMP_START, // 比较运算符指令开始
    // 整数比较
    ____INT_CMP_START, // 整数比较指令开始
    OP_IEQ,            // int == int
    OP_INE,            // int != int
    OP_ILT,            // int < int
    OP_IGT,            // int > int
    OP_ILE,            // int <= int
    OP_IGE,            // int >= int
    ____INT_CMP_END,   // 整数比较指令结束
    // 浮点比较
    ____FLOAT_CMP_START, // 浮点比较指令开始
    OP_FEQ,              // float == float
    OP_FNE,              // float != float
    OP_FLT,              // float < float
    OP_FGT,              // float > float
    OP_FLE,              // float <= float
    OP_FGE,              // float >= float
    ____FLOAT_CMP_END,   // 浮点比较指令结束
    // 字符串比较 (通常只需要 EQ/NE，大小比较较少用但也可支持)
    ____STRING_CMP_START, // 字符串比较指令开始
    OP_SEQ,               // string == string
    OP_SNE,               // string != string
    ____STRING_CMP_END,   // 字符串比较指令结束
    // 对象/引用比较 (比较地址)
    ____OBJ_CMP_START, // 对象比较指令开始
    OP_OEQ,            // object == object
    OP_ONE,            // object != object
    ____OBJ_CMP_END,   // 对象比较指令结束
    ___CMP_END,        // 比较运算符指令结束
    // [LogicExpr]逻辑运算符（&& ||）
    ___LOGIC_START, // 逻辑运算符指令开始
    OP_AND,         //&& 与
    OP_OR,          //|| 或
    ___LOGIC_END,   // 逻辑运算符指令结束
    // [BitExpr]位运算符（& | ^ << >>）
    ___BIT_START, // 位运算符指令开始
    OP_BIT_AND,   //& 与运算
    OP_BIT_OR,    //| 或运算
    OP_BIT_XOR,   //^ 异或运算
    OP_BIT_SHL,   //<< 左移
    OP_BIT_SHR,   //>> 右移
    ___BIT_END,   // 位运算符指令结束
    __BINARY_END, // 二元运算指令结束

    // [UnaryExpr]一元运算（! ~ -）
    __UNARY_START, // 一元运算指令开始
    OP_NOT,        //! 非
    OP_BIT_NOT,    //~ 非运算
    OP_NEG,        //- 取负运算
    __UNARY_END,   // 一元运算指令结束

    _CALC_END, // 运算指令结束

    /*---[CtrlExpr]控制流(跳转指令 + 函数调用指令 + 闭包相关指令)---*/
    _CTRL_START, // 控制流指令开始
    // [JmpExpr]跳转指令（JMP, JNZ, JZ）
    __JMP_START, // 跳转指令开始
    OP_JMP,      // 跳转指令
    OP_JNZ,      // 如果条件不为零则跳转指令(即为真时跳转)
    OP_JZ,       // 如果条件为零则跳转指令(即为假时跳转)
    __JMP_END,   // 跳转指令结束
    // [CallExpr]函数调用指令（CALL, INVOKE, RETURN）
    __CALL_START, // 调用函数指令开始
    OP_CALL,      // 调用函数指令
    OP_INVOKE,    // 调用方法指令
    OP_RETURN,    // 返回指令
    __CALL_END,   // 调用函数指令结束
    // [ClosureExpr]闭包相关指令（CLOSURE, CLOSE_UPVALUE）
    __CLOSURE_START,  // 闭包指令开始
    OP_CLOSURE,       // 闭包指令
    OP_CLOSE_UPVALUE, // 关闭上值指令
    __CLOSURE_END,    // 闭包指令结束
    _CTRL_END,        // 控制流指令结束

    /*---[DataExpr]数据操作（局部变量 + 上值变量 + 全局变量）---*/
    _DATA_START, // 数据操作指令开始
    __VAR_START, // 变量操作指令开始
    // [LocalVarExpr]局部变量（GET_LOCAL, SET_LOCAL）
    OP_GET_LOCAL, // 获取局部变量
    OP_SET_LOCAL, // 设置局部变量
    // [UpvalueVarExpr]上值变量（GET_UPVALUE, SET_UPVALUE）
    OP_GET_UPVALUE, // 获取上值变量
    OP_SET_UPVALUE, // 设置上值变量
    // [GlobalVarExpr]全局变量（GET_GLOBAL, SET_GLOBAL）
    OP_GET_GLOBAL, // 获取全局变量
    OP_SET_GLOBAL, // 设置全局变量
    __VAR_END,     // 变量操作指令结束
    // [ComplexDSExpr]复杂数据结构（MAP + ARRAY）
    __DS_START,      // 复杂数据结构指令开始
    OP_NEW_MAP,      // 新创建一个 map 类型的对象
    OP_SET_MAP,      // 设置 map 类型的对象
    OP_GET_MAP,      // 获取 map 类型的对象
    OP_NEW_ARRAY,    // 新创建一个 array 类型的对象
    OP_PUSH_ARRAY,   // 向 array 类型的对象中压入一个元素
    OP_GET_ARRAY,    // 从 array 类型的对象中获取一个元素
    OP_SET_ARRAY,    // 向array类型的对象中乱序压入一个元素
    OP_GET_PROPERTY, // 获取属性指令
    OP_SET_PROPERTY, // 设置属性指令
    OP_METHOD,       // 调用方法指令
    __DS_END,        // 复杂数据结构指令结束
    // [StackOpExpr]常用数据操作（POP, DUP, SWAP, TRUE, FALSE, NIL）
    __DATA_CTRL_START, // 常用数据操作指令开始
    OP_TRUE,           // 压入 true 常量
    OP_FALSE,          // 压入 false 常量
    OP_NIL,            // 无操作指令
    OP_LOAD_CONST,     // 加载常量
    OP_LOAD_CONST_W,   // 加载宽常量
    OP_MOVE,
    __DATA_CTRL_END, // 常用数据操作指令结束
    _DATA_END,       // 数据操作指令结束

    /*---[SysExpr]系统相关指令（THROW, CATCH）---*/
    _SYS_START, // 系统相关指令开始
    OP_THROW,   // 抛出异常指令
    OP_CATCH,   // 捕获异常指令
    _SYS_END,   // 系统相关指令结束

    /*---计数---*/
    OP_COUNT, // 指令总数
}; // namespace niki::vm

/*---辅助函数区---*/
// 将 OPCODE 转换为 uint8_t
constexpr uint8_t ToInt(OPCODE op) noexcept { return static_cast<uint8_t>(op); }

/*---是否为运算指令---*/
constexpr bool IsCalcOP(OPCODE op) noexcept { return op >= OPCODE::_CALC_START && op <= OPCODE::_CALC_END; }
// 是否为二元运算指令
constexpr bool IsBinaryOP(OPCODE op) noexcept { return op >= OPCODE::__BINARY_START && op <= OPCODE::__BINARY_END; }
// 是否为四则运算指令
constexpr bool IsArithOP(OPCODE op) noexcept { return op >= OPCODE::___ARITH_START && op <= OPCODE::___ARITH_END; }
// 是否为整数四则运算指令
constexpr bool IsIntArithOP(OPCODE op) noexcept {
    return op >= OPCODE::____INT_ARITH_START && op <= OPCODE::____INT_ARITH_END;
}
// 是否为浮点四则运算指令
constexpr bool IsFloatArithOP(OPCODE op) noexcept {
    return op >= OPCODE::____FLOAT_ARITH_START && op <= OPCODE::____FLOAT_ARITH_END;
}

// 是否为比较运算符指令
constexpr bool IsCmpOP(OPCODE op) noexcept { return op >= OPCODE::___CMP_START && op <= OPCODE::___CMP_END; }
// 是否为整数比较指令
constexpr bool IsIntCmpOP(OPCODE op) noexcept {
    return op >= OPCODE::____INT_CMP_START && op <= OPCODE::____INT_CMP_END;
}
// 是否为浮点比较指令
constexpr bool IsFloatCmpOP(OPCODE op) noexcept {
    return op >= OPCODE::____FLOAT_CMP_START && op <= OPCODE::____FLOAT_CMP_END;
}
// 是否为字符串比较指令
constexpr bool IsStringCmpOP(OPCODE op) noexcept {
    return op >= OPCODE::____STRING_CMP_START && op <= OPCODE::____STRING_CMP_END;
}
// 是否为对象比较指令
constexpr bool IsObjCmpOP(OPCODE op) noexcept {
    return op >= OPCODE::____OBJ_CMP_START && op <= OPCODE::____OBJ_CMP_END;
}
// 是否为逻辑运算符指令
constexpr bool IsLogicOP(OPCODE op) noexcept {
    return (op >= OPCODE::___LOGIC_START && op <= OPCODE::___LOGIC_END) || op == OPCODE::OP_NOT;
}
// 是否为位运算符指令
constexpr bool IsBitOP(OPCODE op) noexcept {
    return (op >= OPCODE::___BIT_START && op <= OPCODE::___BIT_END) || op == OPCODE::OP_BIT_NOT;
}
// 是否为一元运算指令
constexpr bool IsUnaryOP(OPCODE op) noexcept { return op >= OPCODE::__UNARY_START && op <= OPCODE::__UNARY_END; }
// 是否为取负运算符指令
constexpr bool IsNegOP(OPCODE op) noexcept { return op == OPCODE::OP_NEG; }

/*---是否为控制流指令---*/
constexpr bool IsCtrlOP(OPCODE op) noexcept { return op >= OPCODE::_CTRL_START && op <= OPCODE::_CTRL_END; }
// 是否为跳转指令
constexpr bool IsJmpOP(OPCODE op) noexcept { return op >= OPCODE::__JMP_START && op <= OPCODE::__JMP_END; }
// 是否为调用函数指令
constexpr bool IsCallOP(OPCODE op) noexcept { return op >= OPCODE::__CALL_START && op <= OPCODE::__CALL_END; }
// 是否为闭包相关指令
constexpr bool IsClosureOP(OPCODE op) noexcept { return op >= OPCODE::__CLOSURE_START && op <= OPCODE::__CLOSURE_END; }

/*---是否为数据操作指令---*/
constexpr bool IsDataOP(OPCODE op) noexcept { return op >= OPCODE::_DATA_START && op <= OPCODE::_DATA_END; }
// 是否为变量操作指令
constexpr bool IsVarOP(OPCODE op) noexcept { return op >= OPCODE::__VAR_START && op <= OPCODE::__VAR_END; }
// 是否为复杂数据结构指令
constexpr bool IsDSOP(OPCODE op) noexcept { return op >= OPCODE::__DS_START && op <= OPCODE::__DS_END; }
// 是否为常用数据操作指令
constexpr bool IsDataCtrlOP(OPCODE op) noexcept {
    return op >= OPCODE::__DATA_CTRL_START && op <= OPCODE::__DATA_CTRL_END;
}

/*---是否为系统相关指令---*/
constexpr bool IsSysOP(OPCODE op) noexcept { return op >= OPCODE::_SYS_START && op <= OPCODE::_SYS_END; }

} // namespace niki::vm
