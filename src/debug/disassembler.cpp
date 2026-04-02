#include "niki/debug/disassembler.hpp"
#include "niki/vm/opcode.hpp"
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iostream>

using namespace niki::vm;
void Disassembler::disassembleChunk(const Chunk &chunk, std::string_view name) {
    std::cout << "==" << name << "==\n";
    for (size_t offset = 0; offset < chunk.code.size();) {
        offset = disassembleInstruction(chunk, offset);
    }
};

size_t Disassembler::disassembleInstruction(const Chunk &chunk, size_t offset) {
    std::cout << std::setfill('0') << std::setw(4) << offset << " ";
    uint32_t line = chunk.lines[offset];
    std::cout << std::setfill(' ') << std::setw(4) << line << " ";
    uint8_t instruction = chunk.code[offset];
    switch (static_cast<OPCODE>(instruction)) {
    // === [ArithExpr] 四则运算 (4字节: Op + dst + src1 + src2) ===
    // 整数运算
    case OPCODE::OP_IADD:
        return registerInstruction("OP_IADD", chunk, offset);
    case OPCODE::OP_ISUB:
        return registerInstruction("OP_ISUB", chunk, offset);
    case OPCODE::OP_IMUL:
        return registerInstruction("OP_IMUL", chunk, offset);
    case OPCODE::OP_IDIV:
        return registerInstruction("OP_IDIV", chunk, offset);
    case OPCODE::OP_IMOD:
        return registerInstruction("OP_IMOD", chunk, offset);
    // 浮点运算
    case OPCODE::OP_FADD:
        return registerInstruction("OP_FADD", chunk, offset);
    case OPCODE::OP_FSUB:
        return registerInstruction("OP_FSUB", chunk, offset);
    case OPCODE::OP_FMUL:
        return registerInstruction("OP_FMUL", chunk, offset);
    case OPCODE::OP_FDIV:
        return registerInstruction("OP_FDIV", chunk, offset);

    // === [CmpExpr] 比较运算符 (4字节: Op + dst + src1 + src2) ===
    // 整数比较
    case OPCODE::OP_IEQ:
        return registerInstruction("OP_IEQ", chunk, offset);
    case OPCODE::OP_INE:
        return registerInstruction("OP_INE", chunk, offset);
    case OPCODE::OP_ILT:
        return registerInstruction("OP_ILT", chunk, offset);
    case OPCODE::OP_IGT:
        return registerInstruction("OP_IGT", chunk, offset);
    case OPCODE::OP_ILE:
        return registerInstruction("OP_ILE", chunk, offset);
    case OPCODE::OP_IGE:
        return registerInstruction("OP_IGE", chunk, offset);
    // 浮点比较
    case OPCODE::OP_FEQ:
        return registerInstruction("OP_FEQ", chunk, offset);
    case OPCODE::OP_FNE:
        return registerInstruction("OP_FNE", chunk, offset);
    case OPCODE::OP_FLT:
        return registerInstruction("OP_FLT", chunk, offset);
    case OPCODE::OP_FGT:
        return registerInstruction("OP_FGT", chunk, offset);
    case OPCODE::OP_FLE:
        return registerInstruction("OP_FLE", chunk, offset);
    case OPCODE::OP_FGE:
        return registerInstruction("OP_FGE", chunk, offset);
    // 字符串比较
    case OPCODE::OP_SEQ:
        return registerInstruction("OP_SEQ", chunk, offset);
    case OPCODE::OP_SNE:
        return registerInstruction("OP_SNE", chunk, offset);
    // 对象比较
    case OPCODE::OP_OEQ:
        return registerInstruction("OP_OEQ", chunk, offset);
    case OPCODE::OP_ONE:
        return registerInstruction("OP_ONE", chunk, offset);

    // === [LogicExpr] 逻辑运算符 (4字节: Op + dst + src1 + src2) ===
    case OPCODE::OP_AND:
        return registerInstruction("OP_AND", chunk, offset);
    case OPCODE::OP_OR:
        return registerInstruction("OP_OR", chunk, offset);

    // === [BitExpr] 位运算符 (4字节: Op + dst + src1 + src2) ===
    case OPCODE::OP_BIT_AND:
        return registerInstruction("OP_BIT_AND", chunk, offset);
    case OPCODE::OP_BIT_OR:
        return registerInstruction("OP_BIT_OR", chunk, offset);
    case OPCODE::OP_BIT_XOR:
        return registerInstruction("OP_BIT_XOR", chunk, offset);
    case OPCODE::OP_BIT_SHL:
        return registerInstruction("OP_BIT_SHL", chunk, offset);
    case OPCODE::OP_BIT_SHR:
        return registerInstruction("OP_BIT_SHR", chunk, offset);

    // === [UnaryExpr] 一元运算 (3字节: Op + dst + src) ===
    case OPCODE::OP_NOT:
        return unaryInstruction("OP_NOT", chunk, offset);
    case OPCODE::OP_BIT_NOT:
        return unaryInstruction("OP_BIT_NOT", chunk, offset);
    case OPCODE::OP_NEG:
        return unaryInstruction("OP_NEG", chunk, offset);

    // === [JmpExpr] 跳转指令 (待定字节宽度，通常是 Op + Offset) ===
    case OPCODE::OP_JMP:
        return jumpInstruction("OP_JMP", 1, chunk, offset);
    case OPCODE::OP_JNZ:
        return jumpInstruction("OP_JNZ", 1, chunk, offset);
    case OPCODE::OP_JZ:
        return jumpInstruction("OP_JZ", 1, chunk, offset);

    // === [CallExpr] 函数调用 ===
    case OPCODE::OP_CALL:
        return simpleInstruction("OP_CALL", offset); // TODO: 具体宽度取决于调用约定
    case OPCODE::OP_INVOKE:
        return simpleInstruction("OP_INVOKE", offset);
    case OPCODE::OP_RETURN:
        return simpleInstruction("OP_RETURN", offset);

    // === [ClosureExpr] 闭包 ===
    case OPCODE::OP_CLOSURE:
        return simpleInstruction("OP_CLOSURE", offset);
    case OPCODE::OP_CLOSE_UPVALUE:
        return simpleInstruction("OP_CLOSE_UPVALUE", offset);

    // === [VarExpr] 变量操作 ===
    case OPCODE::OP_GET_LOCAL:
        return simpleInstruction("OP_GET_LOCAL", offset); // 寄存器机器中通常不需要，变量就在寄存器里
    case OPCODE::OP_SET_LOCAL:
        return simpleInstruction("OP_SET_LOCAL", offset);
    case OPCODE::OP_GET_UPVALUE:
        return simpleInstruction("OP_GET_UPVALUE", offset);
    case OPCODE::OP_SET_UPVALUE:
        return simpleInstruction("OP_SET_UPVALUE", offset);
    case OPCODE::OP_GET_GLOBAL:
        return simpleInstruction("OP_GET_GLOBAL", offset);
    case OPCODE::OP_SET_GLOBAL:
        return simpleInstruction("OP_SET_GLOBAL", offset);

    // === [ComplexDSExpr] 复杂数据结构 ===
    case OPCODE::OP_NEW_MAP:
        return simpleInstruction("OP_NEW_MAP", offset);
    case OPCODE::OP_SET_MAP:
        return simpleInstruction("OP_SET_MAP", offset);
    case OPCODE::OP_GET_MAP:
        return simpleInstruction("OP_GET_MAP", offset);
    case OPCODE::OP_NEW_ARRAY:
        return simpleInstruction("OP_NEW_ARRAY", offset);
    case OPCODE::OP_PUSH_ARRAY:
        return simpleInstruction("OP_PUSH_ARRAY", offset);
    case OPCODE::OP_GET_ARRAY:
        return simpleInstruction("OP_GET_ARRAY", offset);
    case OPCODE::OP_GET_PROPERTY:
        return simpleInstruction("OP_GET_PROPERTY", offset);
    case OPCODE::OP_SET_PROPERTY:
        return simpleInstruction("OP_SET_PROPERTY", offset);
    case OPCODE::OP_METHOD:
        return simpleInstruction("OP_METHOD", offset);

    // === [StackOpExpr] 数据操作与常量装载 ===
    case OPCODE::OP_POP:
        return simpleInstruction("OP_POP", offset); // 寄存器机器废弃
    case OPCODE::OP_DUP:
        return simpleInstruction("OP_DUP", offset); // 寄存器机器废弃
    case OPCODE::OP_SWAP:
        return simpleInstruction("OP_SWAP", offset); // 寄存器机器废弃

    // 常量加载 (2字节: Op + R_dst)
    case OPCODE::OP_TRUE:
        return literalInstruction("OP_TRUE", chunk, offset);
    case OPCODE::OP_FALSE:
        return literalInstruction("OP_FALSE", chunk, offset);
    case OPCODE::OP_NIL:
        return literalInstruction("OP_NIL", chunk, offset);

    // 常量池加载
    case OPCODE::OP_LOAD_CONST:
        return constantInstruction("OP_LOAD_CONST", chunk, offset); // 3字节
    case OPCODE::OP_LOAD_CONST_W:
        return constantWideInstruction("OP_LOAD_CONST_W", chunk, offset); // 4字节

    // 寄存器搬运 (3字节: Op + R_dst + R_src)
    case OPCODE::OP_MOVE:
        return unaryInstruction("OP_MOVE", chunk, offset);

    // === [SysExpr] 系统相关 ===
    case OPCODE::OP_THROW:
        return simpleInstruction("OP_THROW", offset);
    case OPCODE::OP_CATCH:
        return simpleInstruction("OP_CATCH", offset);

    // === 辅助标记忽略 ===
    case OPCODE::_CALC_START:
    case OPCODE::__BINARY_START:
    case OPCODE::___ARITH_START:
    case OPCODE::____INT_ARITH_START:
    case OPCODE::____INT_ARITH_END:
    case OPCODE::____FLOAT_ARITH_START:
    case OPCODE::____FLOAT_ARITH_END:
    case OPCODE::___ARITH_END:
    case OPCODE::___CMP_START:
    case OPCODE::____INT_CMP_START:
    case OPCODE::____INT_CMP_END:
    case OPCODE::____FLOAT_CMP_START:
    case OPCODE::____FLOAT_CMP_END:
    case OPCODE::____STRING_CMP_START:
    case OPCODE::____STRING_CMP_END:
    case OPCODE::____OBJ_CMP_START:
    case OPCODE::____OBJ_CMP_END:
    case OPCODE::___CMP_END:
    case OPCODE::___LOGIC_START:
    case OPCODE::___LOGIC_END:
    case OPCODE::___BIT_START:
    case OPCODE::___BIT_END:
    case OPCODE::__BINARY_END:
    case OPCODE::__UNARY_START:
    case OPCODE::__UNARY_END:
    case OPCODE::_CALC_END:
    case OPCODE::_CTRL_START:
    case OPCODE::__JMP_START:
    case OPCODE::__JMP_END:
    case OPCODE::__CALL_START:
    case OPCODE::__CALL_END:
    case OPCODE::__CLOSURE_START:
    case OPCODE::__CLOSURE_END:
    case OPCODE::_CTRL_END:
    case OPCODE::_DATA_START:
    case OPCODE::__VAR_START:
    case OPCODE::__VAR_END:
    case OPCODE::__DS_START:
    case OPCODE::__DS_END:
    case OPCODE::__DATA_CTRL_START:
    case OPCODE::__DATA_CTRL_END:
    case OPCODE::_DATA_END:
    case OPCODE::_SYS_START:
    case OPCODE::_SYS_END:
    case OPCODE::OP_COUNT:
        std::cout << "Invalid pseudo-opcode encountered.\n";
        return offset + 1;

    default:
        std::cout << "Unknown opcode " << (int)instruction << "\n";
        return offset + 1;
    };
};
size_t Disassembler::simpleInstruction(const char *name, size_t offset) {
    std::cout << name << "\n";
    return offset + 1;
};
// 处理字面量
size_t Disassembler::literalInstruction(const char *name, const Chunk &chunk, size_t offset) {

};
// 一元表达式
size_t Disassembler::unaryInstruction(const char *name, const Chunk &chunk, size_t offset) {};
// 跳转相关指令
size_t Disassembler::jumpInstruction(const char *name, int sign, const Chunk &chunk, size_t offset) {};

size_t Disassembler::registerInstruction(const char *name, const Chunk &chunk, size_t offset) {
    uint8_t dst = chunk.code[offset + 1];
    uint8_t src1 = chunk.code[offset + 2];
    uint8_t src2 = chunk.code[offset + 3];
    std::cout << std::left << std::setw(16) << name << " R" << (int)dst << ", R" << (int)src1 << ", R" << (int)src2
              << "\n";
    return offset + 4;
};

size_t Disassembler::constantInstruction(const char *name, const Chunk &chunk, size_t offset) {
    uint8_t dst = chunk.code[offset + 1];
    uint8_t constant = chunk.code[offset + 2];
    std::cout << std::left << std::setw(16) << name << " R" << (int)dst << ", Const[" << (int)constant << "]\n";
    return offset + 3;
};

size_t Disassembler::constantWideInstruction(const char *name, const Chunk &chunk, size_t offset) {
    uint8_t dst = chunk.code[offset + 1];
    uint8_t high_byte = chunk.code[offset + 2];
    uint8_t low_byte = chunk.code[offset + 3];
    uint16_t constant = (static_cast<uint16_t>(high_byte) << 8) | low_byte;
    std::cout << std::left << std::setw(16) << name << " R" << (int)dst << ", Const[" << (int)constant << "]\n";
    return offset + 4;
};