#include "niki/driver/driver.hpp"
#include "niki/linker/linker.hpp"
#include "niki/runtime/launcher.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include "niki/vm/vm.hpp"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace niki::driver {

static niki::diagnostic::DiagnosticBag makeDriverError(std::string code, std::string message, std::string file = "") {
    niki::diagnostic::DiagnosticBag bag;
    bag.addError(niki::diagnostic::DiagnosticStage::Driver, std::move(code), std::move(message), {.file = file});
    return bag;
}

std::vector<std::string> Driver::collectNkFiles(const std::string &root_dir, const DriverOptions &options) {
    // 目录扫描阶段：只做“找文件”，不做任何编译工作。
    std::vector<std::string> files;
    std::error_code errcode;

    std::filesystem::path root(root_dir);
    if (!std::filesystem::exists(root, errcode) || !std::filesystem::is_directory(root, errcode)) {
        return files;
    }

    auto accept = [&](const std::filesystem::directory_entry &entry) {
        if (!entry.is_regular_file()) {
            return false;
        }
        return entry.path().extension().string() == options.file_ext;
    };
    if (options.recursive_scan) {
        // 递归扫描：适合项目模式（子目录按模块组织）。
        for (std::filesystem::recursive_directory_iterator file_iterator(root, errcode), end;
             file_iterator != end && !errcode; file_iterator.increment(errcode)) {
            if (errcode) {
                break;
            }
            if (accept(*file_iterator)) {
                files.push_back(file_iterator->path().string());
            }
        }
    } else {
        // 非递归扫描：适合单目录脚本模式。
        for (std::filesystem::directory_iterator file_iterator(root, errcode), end;
             file_iterator != end && !errcode; file_iterator.increment(errcode)) {
            if (errcode) {
                break;
            }
            if (accept(*file_iterator)) {
                files.push_back(file_iterator->path().string());
            }
        }
    }

    // 对结果排序，保证每次构建顺序稳定（便于复现与调试）。
    std::sort(files.begin(), files.end());
    return files;
};

std::expected<linker::CompileModule, niki::diagnostic::DiagnosticBag>
Driver::compileOneModule(const std::string &source_path, syntax::GlobalInterner &interner) {
    // 单模块流水线：读文件 -> 扫描 -> 解析 -> 类型检查 -> 编译。
    std::ifstream in(source_path, std::ios::binary);

    if (!in.is_open()) {
        return std::unexpected(makeDriverError("DRIVER_IO_ERROR", "无法打开源文件", source_path));
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    // 1) 词法扫描：把源码切成 token 序列。
    syntax::Scanner scanner(source, source_path);

    std::vector<syntax::Token> tokens;

    while (true) {
        auto token = scanner.scanToken();
        tokens.push_back(token);

        if (token.type == syntax::TokenType::TOKEN_EOF) {
            break;
        }
    }
    auto scannerDiagnostics = scanner.takeDiagnostics();
    if (!scannerDiagnostics.empty()) {
        return std::unexpected(std::move(scannerDiagnostics));
    }

    // 2) 语法解析：构建 AST（ASTPool 使用共享 interner，跨模块 name_id 一致）。
    syntax::ASTPool pool(interner);
    syntax::Parser parser(source, tokens, pool);
    auto parse_result = parser.parse();

    if (!parse_result.diagnostics.empty()) {
        return std::unexpected(std::move(parse_result.diagnostics));
    }

    // 3) 语义检查：写回 node_types，供 compiler 读取。
    semantic::TypeChecker checker;
    auto type_check_result = checker.check(pool, parse_result.root);
    if (!type_check_result.has_value()) {
        return std::unexpected(std::move(type_check_result.error()));
    }

    // 4) 字节码编译：将 AST + 类型表下沉为 chunk。
    syntax::Compiler compiler;
    auto compile_result = compiler.compile(pool, parse_result.root, pool.node_types);
    if (!compile_result.has_value()) {
        return std::unexpected(std::move(compile_result.error()));
    }

    // 5) 组装 CompileModule（给 linker 使用）。
    linker::CompileModule module;
    module.module_name = std::filesystem::path(source_path).stem().string();
    module.source_path = source_path;
    module.init_chunk = std::move(compile_result.value());

    // MVP：导出表先按“定义名ID -> 同ID”占位。
    // 后续可在这里接入 import/export 可见性与重命名规则。

    for (const auto &constant : module.init_chunk.constants) {
        if (constant.type != vm::ValueType::Object || constant.as.object == nullptr) {
            continue;
        }
        auto *object = static_cast<vm::Object *>(constant.as.object);
        if (object->type == vm::ObjType::Function) {
            auto *function_object = static_cast<vm::ObjFunction *>(constant.as.object);
            module.exports[function_object->name_id] = function_object->name_id;
        } else if (object->type == vm::ObjType::StructDef) {
            auto *structDef = static_cast<vm::ObjStructDef *>(constant.as.object);
            module.exports[structDef->name_id] = structDef->name_id;
        }
    }
    return module;
};

std::expected<std::vector<linker::CompileModule>, niki::diagnostic::DiagnosticBag> Driver::compileAll(
    const std::vector<std::string> &files) {
    // 项目级批量编译：共享一个 interner，避免模块间字符串 ID 语义漂移。
    std::vector<linker::CompileModule> modules;
    niki::diagnostic::DiagnosticBag diagnostics;
    syntax::GlobalInterner interner;

    modules.reserve(files.size());

    for (const auto &file : files) {
        auto module_result = compileOneModule(file, interner);
        if (!module_result.has_value()) {
            diagnostics.merge(std::move(module_result.error()));
            continue;
        }
        modules.push_back(std::move(module_result.value()));
    }

    if (!diagnostics.empty()) {
        // 收集到任意模块错误就整体失败，保持“全有或全无”的项目构建语义。
        return std::unexpected(std::move(diagnostics));
    }
    return modules;
};

std::expected<vm::Value, niki::diagnostic::DiagnosticBag> Driver::runProject(const std::string root_dir,
                                                                              const DriverOptions &options) {
    // 总控流程：
    // A. 收集 .nk 文件
    // B. 编译全部模块
    // C. 交给 linker 产出 LinkedProgram
    // D. 交给 launcher 在 VM 中启动
    auto files = collectNkFiles(root_dir, options);
    if (files.empty()) {
        return std::unexpected(makeDriverError("DRIVER_NO_INPUT", "未找到 .nk 文件", root_dir));
    }
    auto compiled = compileAll(files);

    if (!compiled.has_value()) {
        return std::unexpected(compiled.error());
    }

    linker::Linker linker;
    linker::LinkOptions link_options;
    link_options.entry_name = options.entry_name;

    auto linked = linker.link(compiled.value(), link_options);
    if (!linked.has_value()) {
        return std::unexpected(std::move(linked.error()));
    }

    vm::VM vm;
    runtime::Launcher launcher;
    runtime::LaunchOptions launch_options;
    launch_options.call_entry = true;

    auto launch_result = launcher.launchProgram(vm, linked.value(), launch_options);
    if (!launch_result.has_value()) {
        return std::unexpected(std::move(launch_result.error()));
    }
    return launch_result.value();
};
} // namespace niki::driver
