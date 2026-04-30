# 核心层错误码对照与索引（L0 Core）

本文档统一归档 `l0_core` 错误码信息，提供三类结构化视图：
- 错误码归档总表（单一真相源）
- 物理文件结构拆分与归类
- 错误码实际触发位置索引

## 1. 文档约定

- 错误码定义来源：
  - `include/niki/l0_core/diagnostic/diagnostic.hpp`
  - `src/l0_core/diagnostic/diagnostic.cpp`
- 诊断载体：`diagnostic::DiagnosticBag`
- 统一字段：`stage` / `severity` / `code` / `message` / `span`

## 2. 错误码归档总表（统一表结构）

| 阶段 | 事件枚举 | 标准错误码 | 严重级别（默认） | 说明 | 典型触发 |
|---|---|---|---|---|---|
| Scanner | `events::ScannerCode::InvalidToken` | `SCANNER_INVALID_TOKEN` | `error` | 词法层发现非法或不支持 token | 非法字符、未闭合字面量、损坏 token 序列 |
| Parser | `events::ParserCode::GenericError` | `PARSER_ERROR` | `error` | 语法分析失败（当前为通用错误） | 语法不匹配、缺少分号/括号、声明体结构错误 |
| Semantic | `events::SemanticCode::GenericError` | `SEMANTIC_ERROR` | `error` | 语义检查失败（当前为通用错误） | 类型不匹配、符号不可见、非法前置声明、导入解析失败 |
| IR | `events::IRCode::InvalidRoot` | `IR_INVALID_ROOT` | `error` | IR 构建入口根节点非法 | root 失效、root 非 `ModuleDecl/ProgramRoot` |
| IR | `events::IRCode::VerifyFailed` | `IR_VERIFY_FAILED` | `error` | IR 一致性校验失败 | CFG/span/operand/领域附加校验失败 |
| IR | `events::IRCode::LowerFailed` | `IR_LOWER_FAILED` | `error` | IR 降级到字节码失败 | opcode 不可执行、引用越界、编码失败 |
| IR | `events::IRCode::GenericError` | `IR_ERROR` | `error` | IR 通用错误 | Builder 内部通用错误路径 |
| Linker | `events::LinkerCode::DuplicateSymbol` | `LINKER_DUPLICATE_SYMBOL` | `error` | 链接期符号冲突 | 跨模块同名导出冲突 |
| Linker | `events::LinkerCode::MultipleEntry` | `LINKER_MULTIPLE_ENTRY` | `error` | 检测到多个入口 | 多模块同时导出入口函数 |
| Linker | `events::LinkerCode::EntryNotFound` | `LINKER_ENTRY_NOT_FOUND` | `error` | 未找到入口 | 无可执行入口或模块集合为空 |
| Launcher | `events::LauncherCode::InitRuntimeError` | `LAUNCHER_INIT_RUNTIME_ERROR` | `error` | 运行时初始化失败 | VM 初始化或模块装载失败 |
| Launcher | `events::LauncherCode::EntryLookupFailed` | `LAUNCHER_ENTRY_LOOKUP_FAILED` | `error` | 入口查找失败 | 链接产物中入口符号缺失 |
| Launcher | `events::LauncherCode::EntryRuntimeError` | `LAUNCHER_ENTRY_RUNTIME_ERROR` | `error` | 入口执行失败 | 入口执行抛错、解释器运行失败 |
| Driver | `events::DriverCode::IoError` | `DRIVER_IO_ERROR` | `error` | Driver 文件/IO 层失败 | 源文件无法读取、路径不可访问 |
| Driver | `events::DriverCode::NoInput` | `DRIVER_NO_INPUT` | `error` | 无有效输入文件 | 目录扫描后无 `.nk` 输入 |

## 3. 物理文件结构拆分与归类

| 分类 | 文件路径 | 角色 | 维护要求 |
|---|---|---|---|
| 定义层 | `include/niki/l0_core/diagnostic/diagnostic.hpp` | 定义阶段枚举、事件码、事件结构、`DiagnosticBag` 接口 | 新增错误码必须先改这里 |
| 映射层 | `src/l0_core/diagnostic/diagnostic.cpp` | `events::*Code -> 标准错误码字符串` 映射与事件归一化 | 新增错误码必须同步 `codeOf(...)` |
| 渲染层 | `include/niki/l0_core/diagnostic/renderer.hpp` `src/l0_core/diagnostic/renderer.cpp` | 文本渲染与输出格式化 | 禁止编码业务语义分支 |
| 使用层（前端） | `src/l0_core/syntax/*` `src/l0_core/semantic/*` | 语法/语义阶段上报诊断 | 只发事件，不做码映射 |
| 使用层（中后端） | `src/l0_core/ir/*` `src/l0_core/linker/*` `src/l0_core/runtime/*` `src/meta/orchestrator/*` `src/meta/project/*` `src/meta/runtime_host/*` `src/meta/precompile/*` | IR/链接/启动与元编排层上报诊断 | 保持 stage 与 error code 一致 |

## 4. 错误码实际位置索引（调用点）

> 说明：以下为核心触发点索引；同一错误码可能在多处触发。

| 标准错误码 | 主要触发文件 | 代表触发位置/语义 |
|---|---|---|
| `SCANNER_INVALID_TOKEN` | `src/l0_core/syntax/scanner.cpp` | 扫描非法 token、未支持词法形态 |
| `PARSER_ERROR` | `src/l0_core/syntax/parse.cpp` | 解析失败统一上报 |
| `SEMANTIC_ERROR` | `src/l0_core/semantic/type_checker.cpp`, `src/meta/precompile/predeclare_stage.cpp`, `src/meta/precompile/module_context_stage.cpp`, `src/meta/orchestrator/compiler_orchestrator.cpp` | 类型检查失败、预声明/导入解析失败 |
| `IR_INVALID_ROOT` | `src/l0_core/ir/builder_declaration.cpp` | IR build root 非法 |
| `IR_VERIFY_FAILED` | `src/meta/orchestrator/compiler_orchestrator.cpp` | Verify 收到 issue 后聚合上报 |
| `IR_LOWER_FAILED` | `src/meta/orchestrator/compiler_orchestrator.cpp` | Lower 失败统一上报 |
| `IR_ERROR` | `src/l0_core/ir/builder.cpp` | Builder 通用错误路径 |
| `LINKER_DUPLICATE_SYMBOL` | `src/meta/project/project_linker.cpp` | 链接符号冲突 |
| `LINKER_MULTIPLE_ENTRY` | `src/meta/project/project_linker.cpp` | 多入口冲突 |
| `LINKER_ENTRY_NOT_FOUND` | `src/meta/project/project_linker.cpp` | 缺失入口或无模块 |
| `LAUNCHER_INIT_RUNTIME_ERROR` | `src/meta/runtime_host/runtime_host.cpp` | 运行时初始化失败 |
| `LAUNCHER_ENTRY_LOOKUP_FAILED` | `src/meta/runtime_host/runtime_host.cpp` | 入口查找失败 |
| `LAUNCHER_ENTRY_RUNTIME_ERROR` | `src/meta/runtime_host/runtime_host.cpp` | 入口执行失败 |
| `DRIVER_IO_ERROR` | `src/meta/orchestrator/compiler_orchestrator.cpp` | 读取源文件失败 |
| `DRIVER_NO_INPUT` | `src/meta/orchestrator/compiler_orchestrator.cpp` | 无输入源文件 |

## 5. 归档维护规范

- 新增错误码时必须同步更新三处：
  1. `diagnostic.hpp` 中的 `events::*Code`
  2. `diagnostic.cpp` 中 `codeOf(...)` 映射
  3. 本文档“归档总表 + 实际位置索引”
- 不允许只改文案不改码语义；错误码是测试与日志契约的一部分。
- 推荐逐步降低 `GenericError` 占比，为高频失败路径补充专用错误码。
