
#include "niki/vm/vm.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>

using namespace niki::vm;

std::expected<Value, InterpretResult> VM::interpret(const Chunk &chunk, bool is_repl) {
    current_string_pool = &chunk.string_pool;

    // 构造一个顶层的伪函数来容纳脚本字节码
    ObjFunction *scriptFunc = new ObjFunction();
    scriptFunc->name_id = 0; // 顶层脚本没有真正的名字，随便给个0，反正我们靠指针判断
    // 由于是顶层脚本，我们要把传入的chunk拷进去(或者直接在compiler里生成obfuction)
    // 但为了当前兼容，我们先及逆行浅拷贝或借用。
    scriptFunc->chunk = chunk;
    if (scriptFunc->chunk.code.empty()) {
        delete scriptFunc;
        return Value::makeNil();
    }
    frames.clear();
    frames.push_back(CallFrame{
        .function = scriptFunc, .ip = const_cast<uint8_t *>(scriptFunc->chunk.code.data()), .base_register = 0});
    currentFrame = &frames.back();

    // 预先查找 main_id，用于判断 REPL 模式下是否要打印顶层返回值
    uint32_t main_id = -1;
    for (uint32_t i = 0; i < current_string_pool->size(); ++i) {
        if ((*current_string_pool)[i] == "main") {
            main_id = i;
            break;
        }
    }

    std::expected<Value, InterpretResult> result =
        run(is_repl && globals.count(main_id) == 0); // 顶层脚本只在纯REPL无main时打印

    // --- 自动点火：寻找并执行 main 函数 ---
    if (result.has_value()) {
        if (main_id != -1 && globals.count(main_id)) {
            ObjFunction *mainFunc = globals[main_id];
            frames.clear();
            frames.push_back(CallFrame{
                .function = mainFunc, .ip = const_cast<uint8_t *>(mainFunc->chunk.code.data()), .base_register = 0});
            currentFrame = &frames.back();

            // 第二次执行！这次跑的是真正的 main 业务逻辑
            result = run(true); // 业务主函数始终打印返回值（作为MVP演示）
        }
    }

    // 清理顶层伪函数
    delete scriptFunc;
    return result;
};

void VM::runtime_error(const char *format, ...) {
    std::cerr << "Runtime Error:";
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    std::cerr << "\n";

    // 打印调用栈
    for (int i = frames.size() - 1; i >= 0; i--) {
        CallFrame *frame = &frames[i];
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

std::expected<Value, InterpretResult> VM::run(bool should_print) {
    while (true) {
        uint8_t instruction = readByte();
        switch (static_cast<OPCODE>(instruction)) {
        case OPCODE::OP_LOAD_CONST: {
            uint8_t targetReg = readByte();
            uint8_t constIdx = readByte();
            currentRegisters()[targetReg] = readConstant(constIdx);
            break;
        }
        case OPCODE::OP_LOAD_CONST_W: {
            uint8_t targetReg = readByte();
            uint16_t constIdx = readShort();
            currentRegisters()[targetReg] = readConstant(static_cast<uint8_t>(constIdx));
            break;
        }
        case OPCODE::OP_MOVE: {
            uint8_t targetReg = readByte();
            uint8_t srcReg = readByte();
            currentRegisters()[targetReg] = currentRegisters()[srcReg];
            break;
        }
        case OPCODE::OP_IADD: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer + currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_ISUB: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer - currentRegisters()[rightReg].as.integer);
            break;
        }

        case OPCODE::OP_IMUL: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer * currentRegisters()[rightReg].as.integer);
            break;
        }

        case OPCODE::OP_IDIV: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            if (currentRegisters()[rightReg].as.integer == 0) {
                runtime_error("Division by zero.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer / currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_IMOD: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            if (currentRegisters()[rightReg].as.integer == 0) {
                runtime_error("Modulo by zero.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] =
                Value::makeInt(currentRegisters()[leftReg].as.integer % currentRegisters()[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_CONCAT: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();

            Value left = currentRegisters()[leftReg];
            Value right = currentRegisters()[rightReg];

            if (!isString(left) || !isString(right)) {
                runtime_error("Operands must be strings for concatenation '..'.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            ObjString *a = asString(left);
            ObjString *b = asString(right);

            uint32_t new_len = a->length + b->length;
            // 暂时不考虑内存释放，先分配足够大的新字符串
            ObjString *new_str = static_cast<ObjString *>(std::malloc(sizeof(ObjString) + new_len + 1));
            new_str->obj.type = ObjType::String;
            new_str->obj.isMarked = false;
            new_str->length = new_len;

            // 连续的内存拷贝
            std::memcpy(new_str->chars, a->chars, a->length);
            std::memcpy(new_str->chars + a->length, b->chars, b->length);
            new_str->chars[new_len] = '\0';

            currentRegisters()[targetReg] = Value::makeObject(new_str);
            break;
        }
        case OPCODE::OP_NEG: {
            uint8_t targetReg = readByte();
            uint8_t srcReg = readByte();
            if (currentRegisters()[srcReg].type != ValueType::Integer) {
                runtime_error("Operand must be a number.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(-currentRegisters()[srcReg].as.integer);
            break;
        }
        case OPCODE::OP_NOT: {
            uint8_t targetReg = readByte();
            uint8_t srcReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t srcReg = readByte();
            if (currentRegisters()[srcReg].type != ValueType::Integer) {
                runtime_error("Operand must be an integer.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = Value::makeInt(~currentRegisters()[srcReg].as.integer);
            break;
        }
        case OPCODE::OP_BIT_AND: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
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
            uint8_t targetReg = readByte();
            currentRegisters()[targetReg] = Value::makeBool(true);
            break;
        }
        case OPCODE::OP_FALSE: {
            uint8_t targetReg = readByte();
            currentRegisters()[targetReg] = Value::makeBool(false);
            break;
        }
        case OPCODE::OP_NIL: {
            uint8_t targetReg = readByte();
            currentRegisters()[targetReg] = Value::makeNil();
            break;
        }
        case OPCODE::OP_NEW_ARRAY: {
            uint8_t targetReg = readByte();
            uint8_t initial_capacity = readByte();
            ObjArray *array = allocateArray(initial_capacity);
            currentRegisters()[targetReg] = Value::makeObject(array);
            break;
        }
        case OPCODE::OP_PUSH_ARRAY: {
            uint8_t arrayReg = readByte();
            uint8_t valReg = readByte();
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
            uint8_t targetReg = readByte();
            uint8_t arrayReg = readByte();
            uint8_t indexReg = readByte();
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
            int64_t idx = idxVal.as.integer;

            if (idx < 0 || idx >= array->count) {
                runtime_error("Array index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }
            currentRegisters()[targetReg] = array->elements[idx];
            break;
        }
        case OPCODE::OP_SET_ARRAY: {
            uint8_t arrayReg = readByte();
            uint8_t indexReg = readByte();
            uint8_t valReg = readByte();

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
            int64_t idx = idxVal.as.integer;

            if (idx < 0 || idx >= array->count) {
                runtime_error("Array index out of bounds.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            array->elements[idx] = currentRegisters()[valReg];
            break;
        }
        case OPCODE::OP_AND: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();

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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();

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
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();

            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer == currentRegisters()[rightReg].as.integer);
            break;
        }

        // int != int
        case OPCODE::OP_INE: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer != currentRegisters()[rightReg].as.integer);
            break;
        }
        // int < int
        case OPCODE::OP_ILT: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer < currentRegisters()[rightReg].as.integer);
            break;
        }
        // int > int
        case OPCODE::OP_IGT: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer > currentRegisters()[rightReg].as.integer);
            break;
        }
        // int <= int
        case OPCODE::OP_ILE: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer <= currentRegisters()[rightReg].as.integer);
            break;
        }
        // int >= int
        case OPCODE::OP_IGE: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            currentRegisters()[targetReg] =
                Value::makeBool(currentRegisters()[leftReg].as.integer >= currentRegisters()[rightReg].as.integer);
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
                        ObjArray *arr = asArray(result);
                        std::cout << "[Array: size=" << arr->count << " cap=" << arr->capacity << "]";
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
            uint16_t offset = readShort();
            currentFrame->ip += offset;
            break;
        }
        case OPCODE::OP_LOOP: {
            uint16_t offset = readShort();
            currentFrame->ip -= offset;
            break;
        }
        case OPCODE::OP_JZ: {
            uint8_t condReg = readByte();
            uint16_t offest = readShort();

            bool is_false = false;
            if (currentRegisters()[condReg].type == ValueType::Bool) {
                is_false = !currentRegisters()[condReg].as.boolean;
            } else if (currentRegisters()[condReg].type == ValueType::Integer) {
                is_false = (currentRegisters()[condReg].as.integer == 0);
            } else if (currentRegisters()[condReg].type == ValueType::Nil) {
                is_false = true;
            }

            if (is_false) {
                currentFrame->ip += offest;
            }
            break;
        }
        case OPCODE::OP_JNZ: {
            uint8_t condReg = readByte();
            uint16_t offest = readShort();

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
                currentFrame->ip += offest;
            }
            break;
        }

        // --- 全局变量与函数池 ---
        case OPCODE::OP_DEFINE_GLOBAL: {
            uint8_t constIdx = readByte();
            Value funcVal = readConstant(constIdx);
            ObjFunction *func = static_cast<ObjFunction *>(funcVal.as.object);

            // 注册到全局表
            globals[func->name_id] = func;
            break;
        }

        case OPCODE::OP_GET_GLOBAL: {
            uint8_t targetReg = readByte();
            uint8_t constIdx = readByte();
            Value nameIdVal = readConstant(constIdx);
            uint32_t name_id = static_cast<uint32_t>(nameIdVal.as.integer);

            auto it = globals.find(name_id);
            if (it == globals.end()) {
                runtime_error("Undefined global variable or function.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            // 把找到的函数对象装回虚拟寄存器里，供下一步的 OP_CALL 使用
            currentRegisters()[targetReg] = Value::makeObject(it->second);
            break;
        }

        case OPCODE::OP_CALL: {
            uint8_t outReg = readByte();
            uint8_t calleeReg = readByte();
            uint8_t argStartReg = readByte();
            uint8_t argc = readByte();

            Value callee = currentRegisters()[calleeReg];
            if (callee.type != ValueType::Object) {
                runtime_error("Can only call functions");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            // 这里先简化处理，假设object一定是objfuction
            ObjFunction *fuction = static_cast<ObjFunction *>(callee.as.object);

            if (argc != fuction->arity) {
                runtime_error("Expected %d .", fuction->arity, argc);
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            if (frames.size() == 256) {
                // 硬编码最大调用深度
                runtime_error("Stack overflow.");
                return std::unexpected(InterpretResult::RUNTIME_ERROR);
            }

            //--- 核心：滑动寄存器窗口---
            // 下一个函数的base_register = 当前函数的 base_register +参数起始寄存器
            size_t new_base = currentFrame->base_register + argStartReg;
            // 为了安全起见，我们将 outReg 存在某个地方，等被调用函数返回时，把结果写回这里。
            // 因为当 CallFrame 弹出时，我们会丢失 outReg 的信息。
            // 工业级做法通常是将 Caller 的 outReg 保存在 CallFrame 结构中。
            // 这里我们临时借用一个机制：在 CallFrame 里增加 out_register 字段。

            frames.push_back(CallFrame{.function = fuction,
                                       .ip = const_cast<uint8_t *>(fuction->chunk.code.data()),
                                       .base_register = new_base,
                                       .out_register = outReg});
            currentFrame = &frames.back();
            break;
        }
        default:
            runtime_error("Unknown opcode.");
            return std::unexpected(InterpretResult::RUNTIME_ERROR);
        }
    }
};