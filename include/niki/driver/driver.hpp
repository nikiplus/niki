#pragma once
#include "niki/diagnostic/diagnostic.hpp"
#include "niki/linker/linker.hpp"
#include "niki/syntax/global_interner.hpp"
#include "niki/vm/value.hpp"
#include <expected>
#include <string>
#include <vector>

namespace niki::driver {
// Driver 是项目级编排器：目录扫描 -> 单文件编译 -> 多模块链接 -> 启动运行。
// 错误统一使用 DiagnosticBag 上抛，Driver 不再维护独立错误结构体。
struct DriverOptions {
    // 是否递归扫描子目录中的 .nk 文件。
    bool recursive_scan = true;
    // 源文件扩展名过滤（默认 .nk）。
    std::string file_ext = ".nk";
    // 入口函数名（传给 linker 做入口决议）。
    std::string entry_name = "main";
};

class Driver {
  public:
    // 项目总入口：
    // 成功时返回程序最终值；失败时返回按阶段聚合后的错误列表。
    std::expected<vm::Value, niki::diagnostic::DiagnosticBag> runProject(const std::string root_dir,
                                                                          const DriverOptions &options);

  private:
    // 从目录收集待编译文件，返回已排序路径列表（保证构建顺序稳定）。
    std::vector<std::string> collectNkFiles(const std::string &root_dir, const DriverOptions &options);

    // 编译单个模块（scan + parse + typecheck + compile），复用共享 interner 保持 name_id 一致。
    std::expected<linker::CompileModule, niki::diagnostic::DiagnosticBag>
    compileOneModule(const std::string &source_path, syntax::GlobalInterner &interner);

    // 批量编译：尽量继续编译其余文件，最后统一返回模块列表或错误列表。
    std::expected<std::vector<linker::CompileModule>, niki::diagnostic::DiagnosticBag> compileAll(
        const std::vector<std::string> &files);
};
} // namespace niki::driver