#pragma once
#include "niki/vm/chunk.hpp"
#include <cstddef>
#include <string_view>

// 反编译器，用来把compiler压缩出的字节码反编译回可见的文本格式。
namespace niki::vm {
class Disassembler {
  public:
    static void disassembleChunk(const Chunk &chunk, std::string_view name);

  private:
    static size_t disassembleInstruction(const Chunk &chunk, size_t offset);
    static size_t simpleInstruction(const char *name, size_t offset);
    static size_t registerInstruction(const char *name, const Chunk &chunk, size_t offset);
    static size_t constantInstruction(const char *name, const Chunk &chunk, size_t offset);
};
} // namespace niki::vm