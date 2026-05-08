# Niki 分层与 IR 统一契约（L0-L5）

本文档定义 Niki 分层架构的强约束，用于约束核心层边界、插件层语义归一、扩展接口、IR 语义标注与优化阶段位置。

配套文档：
- `docs/diagnostics/core_error_codes.md`（核心层错误码对照与触发语义）

## 1. 核心层依赖约束

### 1.1 强约束
- `l0_core` **不得依赖** `l1_domain` ~ `l5_replay` 的实现设施。
- `l0_core` 只允许依赖通用基础模块（syntax/semantic core/ir core/linker/runtime/vm/diagnostic）与标准库。

### 1.2 允许方式
- 允许 `l0_core` 暴露扩展点接口（hook/registry）。
- 允许 `meta` 编排层负责装配插件层（`l1`~`l5`）并注册到 `l0_core` 扩展点。

### 1.3 物理分层映射（2026-04 增量）
- 语言核（kernel 语义）：仍以 `l0_core` 目录承载核心机制实现。
- 领域扩展（domain）：以 `l1_domain` 承载声明语义/IR/verify 扩展。
- 元编排总层（meta）：统一以 `include/niki/meta` 与 `src/meta` 承载工程编排相关实现：
  - `meta/precompile`：预编译阶段（parse/predeclare/module semantic context）
  - `meta/orchestrator`：项目级编译编排门面
  - 多模块链接与 VM 启动：`l0_core/linker`、`l0_core/runtime`（`Linker` / `Launcher`），由编排层调用而非定义在 `meta`
- 旧兼容头路径 `include/niki/orchestrator|project|runtime_host` 已移除。
- `driver` 目录已下线，不再作为物理层存在。

## 2. 插件语义归一与统一解析

### 2.1 语义归一原则
- 任意插件层（含 `l0` 自身）新增语义，最终都必须降级到统一 `ModuleIR` 表达。
- 后续执行路径必须统一走：
  - `IRBuilder -> Verify -> LowerToChunk -> Linker -> Runtime -> VM`

### 2.2 禁止事项
- 禁止插件层绕过 `ModuleIR` 直接发射 VM 字节码。
- 禁止插件层引入独立执行后端与分叉解析路径。

## 3. 分层扩展接口（Hook Contract）

### 3.1 语义扩展钩子（TypeCheck 层）
- 位置：`include/niki/l0_core/semantic/extensions.hpp`
- 约定：
  - `registerDomainSemanticDeclHandler(...)`
  - `getDomainSemanticDeclHandler()`
- 语义：
  - 插件可接管声明节点检查（如 `component/kits/tag/system/flow`）。
  - `l0` 保留默认分发与回退策略。

### 3.2 IR 校验扩展钩子（Verify 层）
- 位置：`include/niki/l0_core/ir/extensions.hpp`
- 约定：
  - `registerDomainVerifyAppendFn(...)`
  - `getDomainVerifyAppendFn()`
- 语义：
  - 插件可附加领域一致性校验（kits/component/symbol 等）。
  - `l0` verify 负责核心结构不变量（SoA/CFG/operand）。

### 3.3 装配责任
- `meta::orchestrator` 是唯一装配入口，负责注册插件实现。
- 插件初始化必须幂等（可重复调用、无副作用扩散）。

## 4. IR 中保留必要语义标注

### 4.1 目标
- IR 需保留执行与诊断所需的最小语义信息，确保语义不丢失且可验证。

### 4.2 最小保留项
- 声明级标注：
  - 符号种类、名称、所属模块、导出状态（统一真相源）。
- 结构级标注：
  - 函数/块/span、入口块、参数元信息、虚拟寄存器上界。
- 领域级标注（通过扩展承载）：
  - kits 窗口、component 身份、领域导出映射等。
- 诊断级标注：
  - 指令级 source line/column 映射。

### 4.3 设计边界
- IR 只保留“执行必需 + 校验必需 + 诊断必需”的语义，不承载高层语法糖本体。

## 5. 优化阶段统一后置

### 5.1 强约束
- 所有优化统一在 IR 之后处理，不得在 parser/typecheck/builder 中做语义重写式优化。

### 5.2 推荐顺序
1. Build IR
2. Verify（结构与领域一致性收口）
3. IR 优化（统一 Pass 管线）
4. LowerToChunk

### 5.3 允许的前置行为
- 前端可做错误恢复与最小规范化（不改变语义等价类）。
- 禁止在前端做影响可观测行为的“提前优化”。

## 6. 执行检查清单（供评审/CI）

- [ ] `l0_core` 无 `l1`~`l5` 头文件或链接依赖。
- [ ] `scripts/check_layer_boundaries.py` 检查通过（`l0_core` 不得 include `meta/*` 与 `domain` 实现层，适配白名单除外）。
- [ ] 新增语义均有对应 IR 表达与 verify 规则。
- [ ] 插件通过 `meta::orchestrator` 注册，不在 `l0` 直接硬编码调用。
- [ ] 导出与领域状态有唯一真相源，不双写。
- [ ] 优化逻辑仅存在于 IR 后置 Pass。

## 7. 插件未注册行为策略（Mandatory）

### 7.1 默认策略
- 默认采用 `StrictFailFast`。
- 当编译单元使用了某插件层语义，但对应插件未注册时，编译流程必须立即失败并返回诊断。

### 7.2 可选策略
- 可配置 `PermissiveDegrade`（仅用于开发/迁移期）。
- 在该模式下：
  - 系统记录明确告警（含缺失插件名、受影响语义节点）。
  - 受影响语义被禁用，不得静默替换为其他行为。

### 7.3 触发判定
- 由 `driver` 在语义检查前执行“语义需求扫描”：
  - 若 AST 出现 `component/kits/tag/taggroup/system/flow` 等节点，且对应插件未注册，则触发策略判定。

## 8. 扩展接口版本策略（ABI/Capability）

### 8.1 核心版本号
- `l0_core` 必须维护扩展接口版本号：`L0_EXT_ABI_VERSION`（整数递增）。

### 8.2 插件元信息
- 每个插件层需声明以下元信息：
  - `plugin_name`
  - `plugin_version`
  - `min_supported_l0_abi`
  - `max_supported_l0_abi`
  - `capabilities`（如 `semantic_hook`, `ir_builder_hook`, `verify_hook`）

### 8.3 握手规则
- `driver` 注册插件时执行版本握手：
  - 若 `L0_EXT_ABI_VERSION` 不在插件支持区间，按策略处理（默认 fail）。
  - 若能力位缺失（例如需要 `verify_hook` 但未声明），按策略处理。

### 8.4 演进约定
- 扩展接口发生签名或语义不兼容变更时，必须提升 `L0_EXT_ABI_VERSION`。
- 仅新增可选能力位时，可不提升 ABI，但必须更新 capability 文档。

## 9. IR 扩展字段演进规则（Schema Evolution）

### 9.1 版本字段
- `ModuleIR` 及领域扩展段必须带版本信息：
  - `core_ir_schema_version`
  - `domain_ir_schema_version`

### 9.2 新增字段
- 新字段必须提供稳定默认值，保证旧数据可读（backward-compatible read）。

### 9.3 删除字段
- 删除字段需走两阶段：
  1. 标记为 `deprecated` 并保留至少一个发布周期；
  2. 下一周期删除，同时提供迁移说明。

### 9.4 重命名字段
- 禁止直接重命名。必须采用“新增字段 + 迁移 + 废弃旧字段”流程。

### 9.5 兼容处理
- `verify` 前必须执行 schema 兼容检查：
  - 版本不兼容时返回结构化诊断并停止后续流程。
- 迁移层（reader/migrator）应集中实现，不得在业务逻辑中散布版本分支。

## 10. 诊断错误码建议（Plugin/IR/Layer）

### 10.1 插件与分层相关

| 错误码 | 含义 | 建议级别 | 关键上下文字段 |
|---|---|---|---|
| `PluginNotRegistered` | 检测到插件语义被使用，但插件未注册 | `error`（Strict）/ `warning`（Permissive） | `plugin_name`, `required_capability`, `source_span` |
| `PluginCapabilityMissing` | 插件已注册但缺少所需能力位（hook） | `error` | `plugin_name`, `required_capability`, `declared_capabilities` |
| `PluginAbiMismatch` | `L0_EXT_ABI_VERSION` 不在插件支持区间 | `error` | `l0_abi`, `plugin_min_abi`, `plugin_max_abi`, `plugin_version` |
| `PluginRegistrationDuplicate` | 同一插件或同一能力重复注册且冲突 | `error` | `plugin_name`, `hook_name`, `existing_owner` |
| `PluginRegistrationConflict` | 多插件对同一独占 hook 声明冲突 | `error` | `hook_name`, `plugin_a`, `plugin_b` |

### 10.2 IR Schema 与迁移相关

| 错误码 | 含义 | 建议级别 | 关键上下文字段 |
|---|---|---|---|
| `IrSchemaVersionMissing` | 缺失 `core_ir_schema_version` 或 `domain_ir_schema_version` | `error` | `module_name` |
| `CoreIrSchemaIncompatible` | 核心 IR 版本不兼容当前 reader/verifier | `error` | `expected_range`, `actual_version` |
| `DomainIrSchemaIncompatible` | 领域 IR 扩展版本不兼容 | `error` | `plugin_name`, `expected_range`, `actual_version` |
| `IrFieldDeprecatedUsed` | 输入仍使用已废弃字段（兼容窗口内） | `warning` | `field_name`, `removal_version` |
| `IrMigrationFailed` | schema 迁移失败 | `error` | `from_version`, `to_version`, `reason` |

### 10.3 分层契约违规相关

| 错误码 | 含义 | 建议级别 | 关键上下文字段 |
|---|---|---|---|
| `LayerDependencyViolation` | 检测到 `l0_core` 依赖 `l1~l5` 设施 | `error`（CI 门禁） | `from_module`, `to_module`, `symbol_or_header` |
| `BypassIrPipeline` | 绕过统一 IR 管线直接进入 lower/vm | `error` | `module`, `entrypoint` |
| `PreIrOptimizationViolation` | 在 IR 前置阶段执行语义重写式优化 | `error` 或 `warning`（迁移期） | `pass_name`, `stage` |

### 10.4 编码与编号建议

- 命名风格：`PascalCase`（按域前缀分组，如 `Plugin*`, `Ir*`, `Layer*`）。
- 编号区间建议：
  - `3000-3099`：Plugin/Registration
  - `3100-3199`：IR Schema/Migration
  - `3200-3299`：Layer Contract
- 所有诊断记录建议统一字段：
  - `code`, `message`, `severity`, `source_span`, `context(map)`。
