#include "niki/driver/driver.hpp"
#include "niki/linker/linker.hpp"
#include "niki/semantic/type_checker.hpp"
#include "niki/syntax/ast.hpp"
#include "niki/syntax/compiler.hpp"
#include "niki/syntax/parser.hpp"
#include "niki/syntax/scanner.hpp"
#include "niki/syntax/token.hpp"
#include "niki/vm/object.hpp"
#include "niki/vm/value.hpp"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace niki::driver {

static std::string joinTypeErrors(const semantic::TypeCheckErrorResult &errs) {
    std::ostringstream oss;
    for (size_t i = 0; i < errs.errors.size(); ++i) {
        const auto &err = errs.errors[i];
        oss << "[line " << err.line << ", column " << err.column << "] " << err.message;
        if (i + 1 < errs.errors.size()) {
            oss << "|";
        }
    }
    return oss.str();
}

static std::string joinCompileErrors(const syntax::CompileResultError &errs) {
    std::ostringstream oss;
    for (size_t i = 0; i < errs.errors.size(); ++i) {
        const auto &err = errs.errors[i];
        oss << "[line " << err.line << ", column " << err.column << "] " << err.message;
        if (i + 1 < errs.errors.size()) {
            oss << "|";
        }
    }
    return oss.str();
}

std::vector<std::string> Driver::collectNkFiles(const std::string &root_dir, const DriverOptions &options) {
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

    std::sort(files.begin(), files.end());
    return files;
};

std::expected<linker::CompileModule, DriverError> Driver::compileOneModule(const std::string &source_path,
                                                                            syntax::GlobalInterner &interner) {
    std::ifstream in(source_path, std::ios::binary);

    if (!in.is_open()) {
        return std::unexpected(DriverError{DriverErrorCode::IO_ERROR, "无法打开源文件"});
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string source = buffer.str();

    syntax::Scanner scanner(source);

    std::vector<syntax::Token> tokens;

    while (true) {
        auto token = scanner.scanToken();
        tokens.push_back(token);

        if (token.type == syntax::TokenType::TOKEN_ERROR) {
            return std::unexpected(DriverError{DriverErrorCode::SCAN_ERROR, "词法扫描失败", source_path});
        }
        if (token.type == syntax::TokenType::TOKEN_EOF) {
            break;
        }
    }

    syntax::ASTPool pool(interner);
    syntax::Parser parser(source, tokens, pool);
    auto root = parser.parse();

    if (parser.hasError()) {
        return std::unexpected(DriverError{DriverErrorCode::PARSE_ERROR, "语法解析失败", source_path});
    }

    semantic::TypeChecker checker;
    auto type_check_result = checker.check(pool, root);
    if (!type_check_result.has_value()) {
        return std::unexpected(
            DriverError{DriverErrorCode::TYPE_ERROR, joinTypeErrors(type_check_result.error()), source_path});
    }

    syntax::Compiler compiler;
    auto compile_result = compiler.compile(pool, root, pool.node_types);
    if (!compile_result.has_value()) {
        return std::unexpected(
            DriverError{DriverErrorCode::COMPILE_ERROR, joinCompileErrors(compile_result.error()), source_path});
    }

    linker::CompileModule module;
    module.module_name = std::filesystem::path(source_path).stem().string();
    module.source_path = source_path;
    module.init_chunk = std::move(compile_result.value());

    // MVP：导出表先按“定义名ID ->同ID”占位，后续可扩展为更多可见性语义

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

std::expected<std::vector<linker::CompileModule>, std::vector<DriverError>> Driver::compileAll(
    const std::vector<std::string> &files) {
    std::vector<linker::CompileModule> modules;
    std::vector<DriverError> errors;
    syntax::GlobalInterner interner;

    modules.reserve(files.size());

    for (const auto &file : files) {
        auto module_result = compileOneModule(file, interner);
        if (!module_result.has_value()) {
            errors.push_back(module_result.error());
            continue;
        }
        modules.push_back(std::move(module_result.value()));
    }

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }
    return modules;
};

std::expected<vm::Value, std::vector<DriverError>> Driver::runProject(const std::string root_dir,
                                                                      const DriverOptions &options) {
    auto files = collectNkFiles(root_dir, options);
    if (files.empty()) {
        return std::unexpected(std::vector<DriverError>{{DriverErrorCode::SCAN_ERROR, "未找到 .nk 文件", root_dir}});
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
        std::vector<DriverError> errs;
        for (const auto &err : linked.error()) {
            errs.push_back({DriverErrorCode::LINK_ERROR, err.message, err.module_path});
        }
        return std::unexpected(std::move(errs));
    }

    vm::VM vm;
    runtime::Launcher launcher;
    runtime::LaunchOptions launch_options;
    launch_options.call_entry = true;

    auto launch_result = launcher.launchProgram(vm, linked.value(), launch_options);
    if (!launch_result.has_value()) {
        return std::unexpected(
            std::vector<DriverError>{{DriverErrorCode::LAUNCH_ERROR, launch_result.error().message, ""}});
    }
    return launch_result.value();
};
} // namespace niki::driver
