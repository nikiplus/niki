/* vm.cpp —— VM::run 主解释循环与各辅助例程实现。
 *
 * 约定：
 * - 指令解码使用宏 VM_RB_(name) / VM_RS_(name) 从当前帧 `ip` 拉取 8/16 位操作数；失败则直接
 *   `return std::unexpected(RUNTIME_ERROR)`，与 tryReadByte / tryReadShort 对称。
 * - 算术与比较在可能接触 `.as.integer` / `.as.floating` 前经 requireInt64 / requireFloat64，避免类型混淆 UB。
 * - 控制流跳转经 tryJumpForward / tryJumpBackward，防止损坏字节码将 ip 甩出 chunk。
 */
#include "niki/l0_core/vm/vm.hpp"
#include "niki/l0_core/vm/chunk.hpp"
#include "niki/l0_core/vm/object.hpp"
#include "niki/l0_core/vm/opcode.hpp"
#include "niki/l0_core/vm/value.hpp"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>

using namespace niki::vm;

// --- 对外入口：脚本 chunk / 命名函数 ---

std::expected<Value, InterpretResult> VM::executeChunk(const Chunk &chunk, bool should_print) {
    current_string_pool = &chunk.string_pool;

    // 顶层脚本：用伪 ObjFunction 持有 chunk（与旧 interpret 前半段一致；不再在此自动调用 main）
    ObjFunction *scriptFunc = new ObjFunction();
    scriptFunc->name_id = 0;
    scriptFunc->chunk = chunk;
    {
        // 脚本帧的寄存器窗口上界：来自编译器写入的 chunk 元数据；默认按 256 槽保守估计（旧 chunk）。
        uint16_t slots = chunk.max_register_slots;
        if (slots == 0) {
            slots = 256;
        }
        scriptFunc->max_registers = slots;
    }
    if (scriptFunc->chunk.code.empty()) {
        delete scriptFunc;
        return Value::makeNil();
    }
    frames.clear();
    frames.push_back(CallFrame{.function = scriptFunc,
                               .ip = const_cast<uint8_t *>(scriptFunc->chunk.code.data()),
                               .base_register = 0,
                               .out_register = 0});
    currentFrame = &frames.back();

    std::expected<Value, InterpretResult> result = run(should_print);

    delete scriptFunc;
    currentFrame = nullptr;
    return result;
}

std::expected<Value, InterpretResult> VM::executeFunction(ObjFunction *function, bool should_print) {
    if (function == nullptr) {
        return std::unexpected(InterpretResult::RUNTIME_ERROR);
    }
    current_string_pool = &function->chunk.string_pool;

    if (function->chunk.code.empty()) {
        return Value::makeNil();
    }
    frames.clear();
    frames.push_back(CallFrame{.function = function,
                               .ip = const_cast<uint8_t *>(function->chunk.code.data()),
                               .base_register = 0,
                               .out_register = 0});
    currentFrame = &frames.back();

    return run(should_print);
}

// --- 全局符号查询（供 launcher / 测试）---

ObjFunction *VM::lookupGlobalFunctionById(uint32_t id) {
    auto global_function_iter = globals.find(id);
    return global_function_iter == globals.end() ? nullptr : global_function_iter->second;
}

ObjFunction *VM::lookupGlobalFunctionByName(const std::string &name) {
    if (current_string_pool == nullptr) {
        return nullptr;
    }
    for (uint32_t string_index = 0; string_index < current_string_pool->size(); ++string_index) {
        if ((*current_string_pool)[string_index] == name) {
            return lookupGlobalFunctionById(string_index);
        }
    }
    return nullptr;
}

// --- 错误报告 ---

void VM::runtime_error(const char *format, ...) {
    std::cerr << "Runtime Error:";
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    std::cerr << "\n";

    // 打印调用栈
    for (int frame_index = static_cast<int>(frames.size()) - 1; frame_index >= 0; frame_index--) {
        CallFrame *frame = &frames[frame_index];
        ObjFunction *function = frame->function;

        // 计算当前指令在chunk中的偏移
        size_t instruction_offset = frame->ip - function->chunk.code.data() - 1;

        std::cerr << "[line ";
        if (instruction_offset < function->chunk.lines.size()) {
            std::cerr << function->chunk.lines[instruction_offset] << " ,column "
                      << function->chunk.columns[instruction_offset] << "] in ";
        } else {
            std::cerr << "unknown] in ";
        }

        if (function == frames[0].function) {
            std::cerr << "script\n";
        } else {
            const char *func_name = "unknown";
            if (current_string_pool && function->name_id < current_string_pool->size()) {
                func_name = (*current_string_pool)[function->name_id].c_str();
            }
            std::cerr << func_name << "()\n";
        }
    }
};

// --- 常量池与指令安全读取 ---

std::expected<Value, InterpretResult> VM::readConstantByIndex(uint32_t index) {
    if (index >= currentFrame->function->chunk.constants.size()) {
        runtime_error("Constant index out of range: %u", index);
        return std::unexpected(InterpretResult::RUNTIME_ERROR);
    }
    return currentFrame->function->chunk.constants[index];
};

bool VM::tryReadByte(uint8_t *byte_out) {
    // ip 相对 chunk 尾部的剩余字节数必须 ≥1，否则视为截断或损坏字节码。
    const auto &code = currentFrame->function->chunk.code;
    const uint8_t *base = code.data();
    const size_t size = code.size();
    const size_t off = static_cast<size_t>(currentFrame->ip - base);
    if (off >= size) {
        runtime_error("Unexpected end of bytecode.");
        return false;
    }
    *byte_out = *currentFrame->ip++;
    return true;
}

bool VM::tryReadShort(uint16_t *short_out) {
    // 大端 uint16：先校验再前进 ip，避免先越界再读 ip[-2]。
    const auto &code = currentFrame->function->chunk.code;
    const uint8_t *base = code.data();
    const size_t size = code.size();
    const size_t off = static_cast<size_t>(currentFrame->ip - base);
    if (off + 2 > size) {
        runtime_error("Unexpected end of bytecode.");
        return false;
    }
    const uint8_t high_byte = currentFrame->ip[0];
    const uint8_t low_byte = currentFrame->ip[1];
    currentFrame->ip += 2;
    *short_out = static_cast<uint16_t>((static_cast<uint16_t>(high_byte) << 8) | low_byte);
    return true;
}

bool VM::tryJumpForward(uint16_t offset) {
    // 条件/无条件前向跳转：ip 已指向操作数之后，offset 为相对前移字节数。
    const auto &code = currentFrame->function->chunk.code;
    const uint8_t *base = code.data();
    const uint8_t *end = base + code.size();
    const uint8_t *new_ip = currentFrame->ip + static_cast<size_t>(offset);
    if (new_ip > end) {
        runtime_error("Jump target out of range.");
        return false;
    }
    currentFrame->ip = const_cast<uint8_t *>(new_ip);
    return true;
}

bool VM::tryJumpBackward(uint16_t offset) {
    // OP_LOOP：回跳到循环头，目标不得早于 code 首字节。
    const auto &code = currentFrame->function->chunk.code;
    const uint8_t *base = code.data();
    const uint8_t *new_ip = currentFrame->ip - static_cast<size_t>(offset);
    if (new_ip < base) {
        runtime_error("Loop jump target out of range.");
        return false;
    }
    currentFrame->ip = const_cast<uint8_t *>(new_ip);
    return true;
}

// --- 类型守卫：在读取 .as.integer / .as.floating 前校验标签（防损坏 bytecode）---

bool VM::requireInt64(Value left, Value right, int64_t *left_integer, int64_t *right_integer) {
    if (left.type != ValueType::Integer || right.type != ValueType::Integer) {
        runtime_error("Operands must be integers.");
        return false;
    }
    *left_integer = left.as.integer;
    *right_integer = right.as.integer;
    return true;
}

bool VM::requireFloat64(Value left, Value right, double *left_float, double *right_float) {
    if (left.type != ValueType::Float || right.type != ValueType::Float) {
        runtime_error("Operands must be floats.");
        return false;
    }
    *left_float = left.as.floating;
    *right_float = right.as.floating;
    return true;
}

bool VM::requireFloat64(Value value, double *resolved_float) {
    if (value.type != ValueType::Float) {
        runtime_error("Operand must be a float.");
        return false;
    }
    *resolved_float = value.as.floating;
    return true;
}

// --- 主解释循环 ---
// VM_RB_(x) / VM_RS_(x)：声明局部操作数并从 ip 读取；失败则 return RUNTIME_ERROR（见文件头说明）。

std::expected<Value, InterpretResult> VM::run(bool should_print) {
#define VM_RB_(name)                                                                                                   \
    uint8_t name;                                                                                                      \
    if (!tryReadByte(&name))                                                                                           \
    return std::unexpected(InterpretResult::RUNTIME_ERROR)
#define VM_RS_(name)                                                                                                   \
    uint16_t name;                                                                                                     \
    if (!tryReadShort(&name))                                                                                          \
    return std::unexpected(InterpretResult::RUNTIME_ERROR)
    // 本地化热点状态——未来再实现。
    // uint8_t *ip = currentFrame->ip;
    // Value *regs = &stack[currentFrame->base_register];
    while (true) {
        VM_RB_(instruction);
        switch (static_cast<OPCODE>(instruction)) {
            // 各 case：操作数顺序与 opcode.hpp / compiler 发射顺序一致；失败路径统一 return RUNTIME_ERROR。

        case OPCODE::OP_LOAD_CONST: {
            VM_RB_(targetReg);
            VM_RB_(constIdx);
            auto constant = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!constant.has_value()) {
                return std::unexpected(constant.error());
            }
            currentRegisters()[targetReg] = constant.value();
            break;
        }
        case OPCODE::OP_LOAD_CONST_W: {
            VM_RB_(targetReg);
            VM_RS_(constIdx);
            auto constant = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!constant.has_value()) {
                return std::unexpected(constant.error());
            }
            currentRegisters()[targetReg] = constant.value();
            break;
        }
        case OPCODE::OP_MOVE: {
            VM_RB_(targetReg);
            VM_RB_(srcReg);
            currentRegisters()[targetReg] = currentRegisters()[srcReg];
            break;
        }
        case OPCODE::OP_IADD: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeInt(left_integer + right_integer);
            break;
        }
        case OPCODE::OP_ISUB: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeInt(left_integer - right_integer);
            break;
        }

        case OPCODE::OP_IMUL: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeInt(left_integer * right_integer);
            break;
        }

        case OPCODE::OP_IDIV: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            if (right_integer == 0) {
                runtime_error("Division by zero.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(left_integer / right_integer);
            break;
        }
        case OPCODE::OP_IMOD: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            if (right_integer == 0) {
                runtime_error("Modulo by zero.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(left_integer % right_integer);
            break;
        }
        case OPCODE::OP_DICE: {
            VM_RB_(targetReg);
            VM_RB_(countReg);
            VM_RB_(sidesReg);

            Value count_value = currentRegisters()[countReg];
            Value sides_value = currentRegisters()[sidesReg];
            if (count_value.type != ValueType::Integer || sides_value.type != ValueType::Integer) {
                runtime_error("DIce operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            int64_t count = count_value.as.integer;
            int64_t sides = sides_value.as.integer;
            if (count < 0 || sides < 0) {
                runtime_error("Dice operands must be >=0.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            if (sides == 0) {
                runtime_error("Dice sides must be > 0.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            int64_t total = 0;
            for (int64_t i = 0; i < count; ++i) {
                total += (static_cast<int64_t>(std::rand()) % sides) + 1;
            }

            currentRegisters()[targetReg] = Value::makeInt(total);
            break;
        }
        case OPCODE::OP_FADD: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeFloat(left_float + right_float);
            break;
        }
        case OPCODE::OP_FSUB: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeFloat(left_float - right_float);
            break;
        }
        case OPCODE::OP_FMUL: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeFloat(left_float * right_float);
            break;
        }
        case OPCODE::OP_FDIV: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            if (right_float == 0.0) {
                runtime_error("Division by zero.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeFloat(left_float / right_float);
            break;
        }
        case OPCODE::OP_CONCAT: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            if (!isString(left) || !isString(right)) {
                runtime_error("Operands must be strings for concatenation '..'.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjString *left_string = asString(left);
            ObjString *right_string = asString(right);

            uint32_t new_len = left_string->length + right_string->length;
            // 暂时不考虑内存释放，先分配足够大的新字符串
            ObjString *new_str = static_cast<ObjString *>(std::malloc(sizeof(ObjString) + new_len + 1));
            new_str->object_header.type = ObjType::String;
            new_str->object_header.isMarked = false;
            new_str->length = new_len;

            // 连续的内存拷贝
            std::memcpy(new_str->chars, left_string->chars, left_string->length);
            std::memcpy(new_str->chars + left_string->length, right_string->chars, right_string->length);
            new_str->chars[new_len] = '\0';

            currentRegisters()[targetReg] = Value::makeObject(new_str);
            break;
        }
        case OPCODE::OP_NEG: {
            VM_RB_(targetReg);
            VM_RB_(srcReg);
            if (currentRegisters()[srcReg].type != ValueType::Integer) {
                runtime_error("Operand must be a number.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(-currentRegisters()[srcReg].as.integer);
            break;
        }
        case OPCODE::OP_FNEG: {
            VM_RB_(targetReg);
            VM_RB_(srcReg);
            double source_float = 0.0;
            if (!requireFloat64(currentRegisters()[srcReg], &source_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeFloat(-source_float);
            break;
        }
        case OPCODE::OP_NOT: {
            VM_RB_(targetReg);
            VM_RB_(srcReg);
            Value val = currentRegisters()[srcReg];
            bool is_false = false;
            if (val.type == ValueType::Nil)
                is_false = true;
            else if (val.type == ValueType::Bool)
                is_false = !val.as.boolean;
            else if (val.type == ValueType::Integer)
                is_false = (val.as.integer == 0);

            currentRegisters()[targetReg] = Value::makeBool(is_false);
            break;
        }
        case OPCODE::OP_BIT_NOT: {
            VM_RB_(targetReg);
            VM_RB_(srcReg);
            if (currentRegisters()[srcReg].type != ValueType::Integer) {
                runtime_error("Operand must be an integer.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(~currentRegisters()[srcReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_AND: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            if (currentRegisters()[leftReg].type != ValueType::Integer ||
                currentRegisters()[rightReg].type != ValueType::Integer) {
                runtime_error("Operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer & currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_OR: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            if (currentRegisters()[leftReg].type != ValueType::Integer ||
                currentRegisters()[rightReg].type != ValueType::Integer) {
                runtime_error("Operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer | currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_XOR: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            if (currentRegisters()[leftReg].type != ValueType::Integer ||
                currentRegisters()[rightReg].type != ValueType::Integer) {
                runtime_error("Operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer ^ currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_SHL: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            if (currentRegisters()[leftReg].type != ValueType::Integer ||
                currentRegisters()[rightReg].type != ValueType::Integer) {
                runtime_error("Operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer << currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_SHR: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            if (currentRegisters()[leftReg].type != ValueType::Integer ||
                currentRegisters()[rightReg].type != ValueType::Integer) {
                runtime_error("Operands must be integers.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer >> currentRegisters()[rightReg].as.integer);
            break;
        }

        case OPCODE::OP_TRUE: {
            VM_RB_(targetReg);
            currentRegisters()[targetReg] = Value::makeBool(true);
            break;
        }
        case OPCODE::OP_FALSE: {
            VM_RB_(targetReg);
            currentRegisters()[targetReg] = Value::makeBool(false);
            break;
        }
        case OPCODE::OP_NIL: {
            VM_RB_(targetReg);
            currentRegisters()[targetReg] = Value::makeNil();
            break;
        }
        case OPCODE::OP_NEW_MAP: {
            VM_RB_(targetReg);
            VM_RB_(initial_capacity);
            ObjMap *map = allocateMap(initial_capacity);
            currentRegisters()[targetReg] = Value::makeObject(map);
            break;
        }
        case OPCODE::OP_SET_MAP: {
            VM_RB_(mapReg);
            VM_RB_(keyReg);
            VM_RB_(valReg);

            Value mapVal = currentRegisters()[mapReg];
            if (!isMap(mapVal)) {
                runtime_error("Target of set must be a map.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjMap *map = asMap(mapVal);
            mapSet(map, currentRegisters()[keyReg], currentRegisters()[valReg]);
            break;
        }
        case OPCODE::OP_GET_MAP: {
            VM_RB_(targetReg);
            VM_RB_(mapReg);
            VM_RB_(keyReg);

            Value mapVal = currentRegisters()[mapReg];
            if (!isMap(mapVal)) {
                runtime_error("Target of get must be a map.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjMap *map = asMap(mapVal);
            Value out = Value::makeNil();
            if (!mapGet(map, currentRegisters()[keyReg], &out)) {
                runtime_error("Map key not found.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = out;
            break;
        }
        case OPCODE::OP_NEW_ARRAY: {
            VM_RB_(targetReg);
            VM_RB_(initial_capacity);
            ObjArray *array = allocateArray(initial_capacity);
            currentRegisters()[targetReg] = Value::makeObject(array);
            break;
        }
        case OPCODE::OP_PUSH_ARRAY: {
            VM_RB_(arrayReg);
            VM_RB_(valReg);
            Value arrVal = currentRegisters()[arrayReg];
            if (!isArray(arrVal)) {
                runtime_error("Target of push must be an array.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            ObjArray *array = asArray(arrVal);
            if (array->count >= array->capacity) {
                uint32_t new_cap = array->capacity < 8 ? 8 : array->capacity * 2;
                expandArray(array, new_cap);
            }
            array->elements[array->count++] = currentRegisters()[valReg];
            break;
        }
        case OPCODE::OP_GET_ARRAY: {
            VM_RB_(targetReg);
            VM_RB_(arrayReg);
            VM_RB_(indexReg);
            Value arrVal = currentRegisters()[arrayReg];
            Value idxVal = currentRegisters()[indexReg];

            if (!isArray(arrVal)) {
                runtime_error("Target of push must be an array.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            if (idxVal.type != ValueType::Integer) {
                runtime_error("Array index must be an integer.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjArray *array = asArray(arrVal);
            int64_t element_index = idxVal.as.integer;

            if (element_index < 0 || element_index >= array->count) {
                runtime_error("Array index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = array->elements[element_index];
            break;
        }
        case OPCODE::OP_SET_ARRAY: {
            VM_RB_(arrayReg);
            VM_RB_(indexReg);
            VM_RB_(valReg);

            Value arrVal = currentRegisters()[arrayReg];
            Value idxVal = currentRegisters()[indexReg];

            if (!isArray(arrVal)) {
                runtime_error("Target of push must be an array.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            if (idxVal.type != ValueType::Integer) {
                runtime_error("Array index must be an integer.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjArray *array = asArray(arrVal);
            int64_t element_index = idxVal.as.integer;

            if (element_index < 0 || element_index >= array->count) {
                runtime_error("Array index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            array->elements[element_index] = currentRegisters()[valReg];
            break;
        }
        case OPCODE::OP_AND: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            bool l_bool = false;
            if (left.type == ValueType::Bool)
                l_bool = left.as.boolean;
            else if (left.type == ValueType::Integer)
                l_bool = (left.as.integer != 0);

            bool r_bool = false;
            if (right.type == ValueType::Bool)
                r_bool = right.as.boolean;
            else if (right.type == ValueType::Integer)
                r_bool = (right.as.integer != 0);

            currentRegisters()[targetReg] = Value::makeBool(l_bool && r_bool);
            break;
        }
        case OPCODE::OP_OR: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            bool l_bool = false;
            if (left.type == ValueType::Bool)
                l_bool = left.as.boolean;
            else if (left.type == ValueType::Integer)
                l_bool = (left.as.integer != 0);

            bool r_bool = false;
            if (right.type == ValueType::Bool)
                r_bool = right.as.boolean;
            else if (right.type == ValueType::Integer)
                r_bool = (right.as.integer != 0);

            currentRegisters()[targetReg] = Value::makeBool(l_bool || r_bool);
            break;
        }
        // int == int
        case OPCODE::OP_IEQ: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer == right_integer);
            break;
        }

        // int != int
        case OPCODE::OP_INE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer != right_integer);
            break;
        }
        // int < int
        case OPCODE::OP_ILT: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer < right_integer);
            break;
        }
        // int > int
        case OPCODE::OP_IGT: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer > right_integer);
            break;
        }
        // int <= int
        case OPCODE::OP_ILE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer <= right_integer);
            break;
        }
        // int >= int
        case OPCODE::OP_IGE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            int64_t left_integer = 0;
            int64_t right_integer = 0;
            if (!requireInt64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_integer, &right_integer))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_integer >= right_integer);
            break;
        }
        case OPCODE::OP_FEQ: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float == right_float);
            break;
        }
        case OPCODE::OP_FNE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float != right_float);
            break;
        }
        case OPCODE::OP_FLT: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float < right_float);
            break;
        }
        case OPCODE::OP_FGT: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float > right_float);
            break;
        }
        case OPCODE::OP_FLE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float <= right_float);
            break;
        }
        case OPCODE::OP_FGE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);
            double left_float = 0.0;
            double right_float = 0.0;
            if (!requireFloat64(currentRegisters()[leftReg], currentRegisters()[rightReg], &left_float, &right_float))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            currentRegisters()[targetReg] = Value::makeBool(left_float >= right_float);
            break;
        }
        case OPCODE::OP_RETURN: {
            Value result = currentRegisters()[0];

            if (frames.size() == 1) {
                if (should_print) {
                    std::cout << ">>> Expr Result: ";
                    if (result.type == ValueType::Integer) {
                        std::cout << result.as.integer;
                    } else if (result.type == ValueType::Bool) {
                        std::cout << (result.as.boolean ? "true" : "false");
                    } else if (result.type == ValueType::Nil) {
                        std::cout << "nil";
                    } else if (isString(result)) {
                        std::cout << "\"" << asString(result)->chars << "\"";
                    } else if (isArray(result)) {
                        ObjArray *array_object = asArray(result);
                        std::cout << "[Array: size=" << array_object->count << " cap=" << array_object->capacity << "]";
                    } else {
                        std::cout << "[Object]";
                    }
                    std::cout << "\n";
                }
                return result;
            }
            uint8_t outReg = currentFrame->out_register;
            frames.pop_back();

            currentFrame = &frames.back();

            currentRegisters()[outReg] = result;
            break;
        }
        case OPCODE::OP_JMP: {
            VM_RS_(offset);
            if (!tryJumpForward(offset))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            break;
        }
        case OPCODE::OP_LOOP: {
            VM_RS_(offset);
            if (!tryJumpBackward(offset))
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            break;
        }
        case OPCODE::OP_JZ: {
            VM_RB_(condReg);
            VM_RS_(offset);

            bool is_false = false;
            if (currentRegisters()[condReg].type == ValueType::Bool) {
                is_false = !currentRegisters()[condReg].as.boolean;
            } else if (currentRegisters()[condReg].type == ValueType::Integer) {
                is_false = (currentRegisters()[condReg].as.integer == 0);
            } else if (currentRegisters()[condReg].type == ValueType::Nil) {
                is_false = true;
            }

            if (is_false) {
                if (!tryJumpForward(offset))
                    return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            break;
        }
        case OPCODE::OP_JNZ: {
            VM_RB_(condReg);
            VM_RS_(offset);

            bool is_true = false;

            if (currentRegisters()[condReg].type == ValueType::Bool) {
                is_true = currentRegisters()[condReg].as.boolean;
            } else if (currentRegisters()[condReg].type == ValueType::Integer) {
                is_true = (currentRegisters()[condReg].as.integer != 0);
            } else if (currentRegisters()[condReg].type == ValueType::Nil) {
                is_true = false;
            } else {
                is_true = true;
            }

            if (is_true) {
                if (!tryJumpForward(offset))
                    return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            break;
        }

        // --- 全局定义与解析（与 linker 之后 init chunk 配合）---
        case OPCODE::OP_DEFINE_GLOBAL: {
            VM_RB_(constIdx);
            auto funcVal = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!funcVal.has_value()) {
                return std::unexpected(funcVal.error());
            }

            // 安全反序列化：检查 object 的 type 来分类存放
            Object *object = static_cast<Object *>(funcVal.value().as.object);
            if (object != nullptr && object->type == ObjType::StructDef) {
                global_objects[static_cast<ObjStructDef *>(static_cast<void *>(object))->name_id] = object;
            } else if (object != nullptr && object->type == ObjType::Function) {
                globals[static_cast<ObjFunction *>(static_cast<void *>(object))->name_id] =
                    static_cast<ObjFunction *>(static_cast<void *>(object));
            } else {
                runtime_error("Invalid global definition type.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            break;
        }

        case OPCODE::OP_DEFINE_GLOBAL_W: {
            VM_RS_(constIdx);
            auto funcVal = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!funcVal.has_value()) {
                return std::unexpected(funcVal.error());
            }
            Object *object = static_cast<Object *>(funcVal.value().as.object);
            if (object != nullptr && object->type == ObjType::StructDef) {
                global_objects[static_cast<ObjStructDef *>(static_cast<void *>(object))->name_id] = object;
            } else if (object != nullptr && object->type == ObjType::Function) {
                globals[static_cast<ObjFunction *>(static_cast<void *>(object))->name_id] =
                    static_cast<ObjFunction *>(static_cast<void *>(object));
            } else {
                runtime_error("Invalid global definition type.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            break;
        }

        case OPCODE::OP_GET_GLOBAL: {
            VM_RB_(targetReg);
            VM_RB_(constIdx);
            auto nameIdVal = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!nameIdVal.has_value()) {
                return std::unexpected(nameIdVal.error());
            }
            uint32_t name_id = static_cast<uint32_t>(nameIdVal.value().as.integer);

            // 先查函数，再查结构体蓝图
            auto global_function_iter = globals.find(name_id);
            if (global_function_iter != globals.end()) {
                currentRegisters()[targetReg] = Value::makeObject(global_function_iter->second);
            } else {
                auto global_object_iter = global_objects.find(name_id);
                if (global_object_iter != global_objects.end()) {
                    currentRegisters()[targetReg] = Value::makeObject(global_object_iter->second);
                } else {
                    runtime_error("Undefined global variable or function.");
                    return std::unexpected(InterpretResult::RUNTIME_ERROR);
                }
            }
            break;
        }

        case OPCODE::OP_GET_GLOBAL_W: {
            VM_RB_(targetReg);
            VM_RS_(constIdx);
            auto nameIdVal = readConstantByIndex(static_cast<uint32_t>(constIdx));
            if (!nameIdVal.has_value()) {
                return std::unexpected(nameIdVal.error());
            }
            uint32_t name_id = static_cast<uint32_t>(nameIdVal.value().as.integer);

            auto global_function_iter = globals.find(name_id);
            if (global_function_iter != globals.end()) {
                currentRegisters()[targetReg] = Value::makeObject(global_function_iter->second);
            } else {
                auto global_object_iter = global_objects.find(name_id);
                if (global_object_iter != global_objects.end()) {
                    currentRegisters()[targetReg] = Value::makeObject(global_object_iter->second);
                } else {
                    runtime_error("Undefined global variable or function.");
                    return std::unexpected(InterpretResult::RUNTIME_ERROR);
                }
            }
            break;
        }

        case OPCODE::OP_NEW_INSTANCE: {
            VM_RB_(outReg);
            VM_RB_(calleeReg);
            VM_RB_(argStartReg);
            VM_RB_(argc);

            Value callee = currentRegisters()[calleeReg];
            if (!isStructDef(callee)) {
                runtime_error("Can only instantiate struct definitions.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            ObjStructDef *struct_definition = static_cast<ObjStructDef *>(callee.as.object);
            if (struct_definition->field_count != argc) {
                runtime_error("Struct instance argument count mismatch.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            const size_t arg_base = currentFrame->base_register + static_cast<size_t>(argStartReg);
            if (arg_base + static_cast<size_t>(argc) > stack_capacity) {
                runtime_error("Struct constructor argument registers out of stack range.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjInstance *instance = allocateInstance(struct_definition);
            for (uint8_t argument_index = 0; argument_index < argc; ++argument_index) {
                instance->fields[argument_index] = currentRegisters()[argStartReg + argument_index];
            }
            // 实例拥有自己的堆内存，因此我们目前将所有新实例视为主权对象（生命周期开始）
            currentRegisters()[outReg] = Value::makeObject(instance);
            break;
        }

        case OPCODE::OP_GET_FIELD: {
            VM_RB_(outReg);
            VM_RB_(instanceReg);
            VM_RB_(fieldIndex);

            Value instanceVal = currentRegisters()[instanceReg];
            if (!isInstance(instanceVal)) {
                runtime_error("Can only get fields from struct instances.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            ObjInstance *instance = static_cast<ObjInstance *>(instanceVal.as.object);
            if (fieldIndex >= instance->struct_definition->field_count) {
                runtime_error("Field index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[outReg] = instance->fields[fieldIndex];
            break;
        }

        case OPCODE::OP_SET_FIELD: {
            VM_RB_(instanceReg);
            VM_RB_(fieldIndex);
            VM_RB_(valueReg);

            Value instanceVal = currentRegisters()[instanceReg];
            if (!isInstance(instanceVal)) {
                runtime_error("Can only set fields on struct instances.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            ObjInstance *instance = static_cast<ObjInstance *>(instanceVal.as.object);
            if (fieldIndex >= instance->struct_definition->field_count) {
                runtime_error("Field index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            instance->fields[fieldIndex] = currentRegisters()[valueReg];
            break;
        }

        case OPCODE::OP_CALL: {
            VM_RB_(outReg);
            VM_RB_(calleeReg);
            VM_RB_(argStartReg);
            VM_RB_(argc);

            Value callee = currentRegisters()[calleeReg];
            if (!isFunction(callee)) {
                runtime_error("Can only call functions");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjFunction *callee_function = asFunction(callee);

            if (argc != callee_function->arity) {
                runtime_error("Expected %u arguments, got %u.", static_cast<unsigned>(callee_function->arity),
                              static_cast<unsigned>(argc));
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            if (frames.size() == 256) {
                // 硬编码最大调用深度
                runtime_error("Stack overflow.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            //--- 核心：滑动寄存器窗口---
            // 下一个函数的base_register = 当前函数的 base_register +参数起始寄存器
            const size_t new_base = currentFrame->base_register + static_cast<size_t>(argStartReg);
            const size_t arg_end = new_base + static_cast<size_t>(argc);
            if (arg_end > stack_capacity) {
                runtime_error("Call argument registers out of stack range.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            uint16_t callee_slots = callee_function->max_registers;
            if (callee_slots == 0) {
                callee_slots = 256;
            }
            if (new_base + static_cast<size_t>(callee_slots) > stack_capacity) {
                runtime_error("Callee register window exceeds physical stack.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            // 为了安全起见，我们将 outReg 存在某个地方，等被调用函数返回时，把结果写回这里。
            // 因为当 CallFrame 弹出时，我们会丢失 outReg 的信息。
            // 工业级做法通常是将 Caller 的 outReg 保存在 CallFrame 结构中。
            // 这里我们临时借用一个机制：在 CallFrame 里增加 out_register 字段。

            frames.push_back(CallFrame{.function = callee_function,
                                       .ip = const_cast<uint8_t *>(callee_function->chunk.code.data()),
                                       .base_register = new_base,
                                       .out_register = outReg});
            currentFrame = &frames.back();
            break;
        }
        case OPCODE::OP_FREE: {
            VM_RB_(targetReg);
            Value val = currentRegisters()[targetReg];
            if (val.type == ValueType::Object && val.as.object != nullptr) {
                // 根据具体的对象类型，执行彻底的物理销毁
                Object *object = static_cast<Object *>(val.as.object);
                switch (object->type) {
                case ObjType::Function:
                    // Function 对象在全局池中，不应被局部 FREE；目前安全起见，什么也不做。
                    break;
                case ObjType::String:
                    std::free(val.as.object); // 柔性数组，直接 free 头指针即可
                    break;
                case ObjType::Array: {
                    ObjArray *array_object = asArray(val);
                    if (array_object->elements != nullptr) {
                        std::free(array_object->elements); // 释放分离的数据块
                    }
                    std::free(array_object); // 释放头
                    break;
                }
                case ObjType::Map: {
                    ObjMap *map = asMap(val);
                    if (map->entries != nullptr) {
                        std::free(map->entries);
                    }
                    std::free(map);
                    break;
                }
                case ObjType::Instance:
                    std::free(val.as.object); // 柔性数组，直接 free 头指针
                    break;
                case ObjType::StructDef:
                    // 蓝图存活在全局区，通常在虚拟机销毁时统一释放，不应被局部变量 FREE
                    break;
                }
            }
            // 释放后，将寄存器清空（置为 NIL），防止重复释放或幽灵访问
            currentRegisters()[targetReg] = Value::makeNil();
            break;
        }
        case OPCODE::OP_SEQ: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            // SEQ只处理sring == string
            if (!isString(left) || !isString(right)) {
                runtime_error("OP_SEQ expects string operands.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            // 字符串按内容比较
            currentRegisters()[targetReg] = Value::makeBool(valueKeyEquals(left, right));
            break;
        }
        case OPCODE::OP_SNE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            if (!isString(left) || !isString(right)) {
                runtime_error("OP_SNE expects string operands.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeBool(!valueKeyEquals(left, right));
            break;
        }

        case OPCODE::OP_OEQ: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            if (left.type != ValueType::Object || right.type != ValueType::Object) {
                runtime_error("OP_ONQ expects string operands.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeBool(!valueKeyEquals(left, right));
            break;
        }
        case OPCODE::OP_ONE: {
            VM_RB_(targetReg);
            VM_RB_(leftReg);
            VM_RB_(rightReg);

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            if (left.type != ValueType::Object || right.type != ValueType::Object) {
                runtime_error("OP_ONE expects string operands.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeBool(!valueKeyEquals(left, right));
            break;
        }
        case OPCODE::OP_INVOKE:
        case OPCODE::OP_GET_PROPERTY:
        case OPCODE::OP_SET_PROPERTY:
        case OPCODE::OP_METHOD:
            runtime_error("Opcode not implemented yet.");
            return std::unexpected(InterpretResult::RUNTIME_ERROR);
#undef VM_RB_
#undef VM_RS_
        default:
            runtime_error("Unknown opcode.");
            return std::unexpected(InterpretResult::RUNTIME_ERROR);
        }
    }
};
