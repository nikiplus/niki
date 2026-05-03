#include "niki/l0_core/linker/linker_facade.hpp"
#include "niki/meta/project/project_linker.hpp"

namespace niki::linker {

std::expected<LinkedProgram, niki::diagnostic::DiagnosticBag> Linker::link(const std::vector<CompileModule> &modules,
                                                                           const LinkOptions &options) {
    meta::project::ProjectLinker project_linker;
    return project_linker.link(modules, options);
}
} // namespace niki::linker
