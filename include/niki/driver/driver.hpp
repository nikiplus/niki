#pragma once
#include "niki/diagnostic/diagnostic.hpp"
#include "niki/linker/linker.hpp"
#include "niki/semantic/global_compilation.hpp"
#include "niki/semantic/global_symbol_table.hpp"
#include "niki/semantic/global_type_arena.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/global_interner.hpp"
#include "niki/vm/chunk.hpp"
#include "niki/vm/value.hpp"
#include <expected>
#include <string>
#include <vector>

namespace niki::driver {

// 与 Driver::parseOneUnit 中扫描+解析阶段一致：要求 unit.source、unit.source_path 已设置。
std::expected<void, diagnostic::DiagnosticBag> parseIntoCompilationUnit(GlobalCompilationUnit &unit);

// 对单个模块执行 Pass-2 预声明（结构体/函数签入 GlobalTypeArena 与 GlobalSymbolTable）。
std::expected<void, diagnostic::DiagnosticBag> predeclareSingleUnit(const GlobalCompilationUnit &unit,
                                                                    GlobalTypeArena &global_arena,
                                                                    GlobalSymbolTable &global_symbols);

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
    std::expected<vm::Value, diagnostic::DiagnosticBag> runProject(const std::string root_dir,
                                                                   const DriverOptions &options);

  private:
    // 从目录收集待编译文件，返回已排序路径列表（保证构建顺序稳定）。
    std::vector<std::string> collectNkFiles(const std::string &root_dir, const DriverOptions &options);

    // 编译单个模块（scan + parse + typecheck + compile），复用共享 interner 保持 name_id 一致。
    // 之前这里我们使用一个compileOneModule方法来解析整个module，但现在我们将其拆分。
    // 1)读取并解析单格文件
    std::expected<GlobalCompilationUnit, diagnostic::DiagnosticBag> parseOneUnit(const std::string &source_path,
                                                                                 syntax::GlobalInterner &interner);
    // 2)单元及语义检查
    std::expected<semantic::TypeCheckResult, diagnostic::DiagnosticBag> typeCheckUnit(
        GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols);
    // 3)单元级字节码编译
    std::expected<Chunk, diagnostic::DiagnosticBag> compileUnitChunk(const GlobalCompilationUnit &unit,
                                                                     GlobalTypeArena &global_arena,
                                                                     GlobalSymbolTable &global_symbols);
    // 4)将编译结果组装为linker：：compileModule
    linker::CompileModule buildCompileModule(std::string source_path, Chunk chunk);
    // 单元编译：compile->module（typecheck 由 Pass-3 独立完成）
    std::expected<linker::CompileModule, diagnostic::DiagnosticBag> compileParsedUnit(
        GlobalCompilationUnit &unit, GlobalTypeArena &global_arena, GlobalSymbolTable &global_symbols);
    //  批量编译：尽量继续编译其余文件，最后统一返回模块列表或错误列表。
    std::expected<std::vector<linker::CompileModule>, diagnostic::DiagnosticBag> compileAll(
        const std::vector<std::string> &files);
    std::expected<void, diagnostic::DiagnosticBag> predeclareAllUnits(const std::vector<GlobalCompilationUnit> &units,
                                                                      GlobalTypeArena &global_arena,
                                                                      GlobalSymbolTable &global_symbols);
};
} // namespace niki::driver