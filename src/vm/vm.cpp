
#include "niki/vm/vm.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/opcode.hpp"
#include "niki/vm/value.hpp"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>

using namespace niki::vm;

InterpretResult VM::interpret(const Chunk &chunk) {
    currentChunk = &chunk;
    if (currentChunk->code.empty()) {
        return InterpretResult::OK;
    }
    ip = const_cast<uint8_t *>(currentChunk->code.data());
    return run();
};

void VM::runtime_error(const char *format, ...) {
    std::cerr << "Runtime Error:";
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    std::cerr << "\n";

    size_t instruction_offset = instructionStart - currentChunk->code.data();
    if (instruction_offset < currentChunk->lines.size()) {
        uint32_t line = currentChunk->lines[instruction_offset];
        uint32_t col = currentChunk->columns[instruction_offset];
        std::cerr << "[Line:" << line << ",Column:" << col << "]in script\n";
    } else {
        std::cerr << "[Unknown Location]in script\n";
    }
};

InterpretResult VM::run() {
    while (true) {
        instructionStart = ip;
        uint8_t instruction = readByte();
        switch (static_cast<OPCODE>(instruction)) {
        case OPCODE::OP_LOAD_CONST: {
            uint8_t targetReg = readByte();
            uint8_t constIdx = readByte();
            registers[targetReg] = readConstant(constIdx);
            break;
        }
        case OPCODE::OP_LOAD_CONST_W: {
            uint8_t targetReg = readByte();
            uint16_t constIdx = readShort();
            registers[targetReg] = currentChunk->constants[constIdx];
            break;
        }
        case OPCODE::OP_MOVE: {
            uint8_t targetReg = readByte();
            uint8_t srcReg = readByte();
            registers[targetReg] = registers[srcReg];
            break;
        }
        case OPCODE::OP_IADD: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            registers[targetReg] = Value::makeInt(registers[leftReg].as.integer + registers[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_ISUB: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            registers[targetReg] = Value::makeInt(registers[leftReg].as.integer - registers[rightReg].as.integer);
            break;
        }

        case OPCODE::OP_IMUL: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            registers[targetReg] = Value::makeInt(registers[leftReg].as.integer * registers[rightReg].as.integer);
            break;
        }

        case OPCODE::OP_IDIV: {
            uint8_t targetReg = readByte();
            uint8_t leftReg = readByte();
            uint8_t rightReg = readByte();
            if (registers[rightReg].as.integer == 0) {
                runtime_error("Division by zero.");
                return InterpretResult::RUNTIME_ERROR;
            }
            registers[targetReg] = Value::makeInt(registers[leftReg].as.integer / registers[rightReg].as.integer);
            break;
        }
        case OPCODE::OP_RETURN: {
            // 打印出寄存器 0 的值，代表程序的最终计算结果。
            std::cout << ">>> Expr Result: " << registers[0].as.integer << "\n";
            return InterpretResult::OK;
        }
        default:
            runtime_error("Unknown opcode.");
            return InterpretResult::RUNTIME_ERROR;
        }
    }
};