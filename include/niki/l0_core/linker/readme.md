# Linker 模块说明

`linker` 负责将多个已编译模块组合成可执行程序。

## 职责

- 汇总 `CompileModule` 集合
- 建立导出/入口解析关系
- 产出 `LinkedProgram`

## 模块内数据流

```mermaid
graph LR
    MOD_DRIVER[driver]
    MOD_RUNTIME[runtime]
    MOD_DIAG[diagnostic]

    subgraph LKM[linker module]
        STAGE_RESOLVE_SYMBOLS[Resolve Symbols]
        STAGE_RESOLVE_ENTRY[Resolve Entry]
        STAGE_MERGE_PROGRAM[Merge Program]
        STAGE_RESOLVE_SYMBOLS --> STAGE_RESOLVE_ENTRY --> STAGE_MERGE_PROGRAM
    end

    MOD_DRIVER -->|IN: CompileModule[] + LinkOptions| STAGE_RESOLVE_SYMBOLS
    STAGE_MERGE_PROGRAM -->|OUT: LinkedProgram| MOD_RUNTIME
    STAGE_MERGE_PROGRAM -->|OUT: link result| MOD_DRIVER

    STAGE_RESOLVE_SYMBOLS -->|OUT: duplicate/missing symbol| MOD_DIAG
    STAGE_RESOLVE_ENTRY -->|OUT: entry resolve errors| MOD_DIAG
```

## 数据边界

- 输入：来自编译阶段的模块产物（包含 `Chunk` 与导出信息）
- 输出：`LinkedProgram`（供 runtime 装载）

## 模块间依赖

- 依赖模块
  - `ir`（间接）
    - 消费 IR 降级后并由 Driver 封装的模块产物（`Chunk + exports`）。
  - `diagnostic`
    - 链接阶段错误（重复导出、入口缺失等）统一进入 `DiagnosticBag`。
- 被依赖模块
  - `driver`：调用 `Linker.link` 组装最终程序。
  - `runtime`：消费 `LinkedProgram` 执行。

## 阶段接口（对外）

- Link
  - 输入：`std::vector<CompileModule>` + Link 选项（入口名等）
  - 输出：`LinkedProgram` 或链接诊断

## 接口契约（输入/输出/失败语义）

- Linker（`Linker::link`）
  - 输入对象：`std::vector<CompileModule>`、`LinkOptions`
  - 输出对象：`std::expected<LinkedProgram, DiagnosticBag>`
  - 失败语义：出现符号冲突、入口歧义或入口缺失时返回 `unexpected(DiagnosticBag)`，不生成可执行产物
  - 错误码来源：`diagnostic` 模块内部映射（事件码：`diagnostic::events::LinkerCode`）

## 主要文件

- `linker/linker_facade.hpp`
- `src/l0_core/linker/linker_facade.cpp`

标准实现：`src/l0_core/linker/linker_facade.cpp`（`Linker::link`）。

## 当前实现结构（2026-04）

- `linker` 由 `driver` 在项目级编译完成后统一调度。
- 输入为多个 `CompileModule`，输出单一 `LinkedProgram` 给 `runtime`。
- 链接失败统一以 `DiagnosticBag` 返回，不产生部分可执行产物。
- 当前链接职责聚焦符号解析与入口决议，不承担语义类型校验。
- 模块语义与可见性扩展愿景见：`docs/architecture/module_link_feature_vision.md`。
