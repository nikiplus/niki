#pragma once
#include "niki/vm/chunk.hpp"
#include <string>

namespace niki::vm {

// 函数对象：包含字节码、常量池、行号信息以及函数的元数据
struct ObjFunction {
    size_t arity = 0;  // 参数个数
    niki::Chunk chunk; // 属于这个函数的独立字节码块
    std::string name;  // 函数名 (顶层脚本可为空)
};

} // namespace niki::vm