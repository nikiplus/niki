#include "niki/l0_core/linker/linker.hpp"
#include "niki/meta/project/project_linker.hpp"

namespace niki::linker {

std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> Linker::link(const std::vector<CompileModule> &modules,
                                                                            const LinkOptions &options) {
    meta::project::ProjectLinker project_linker;
    return project_linker.link(modules, options);
}

// 预留接口：MVP 阶段暂不启用。
// 这些接口是后续“真链接器”落地的位置（重映射、重定位、合并）。
bool Linker::mergeStringPools() { return true; }
bool Linker::remapChunkOperands() { return true; }
bool Linker::resolveSymbols() { return true; }
Chunk Linker::mergeInitChunks() { return Chunk{}; }
} // namespace niki::linker