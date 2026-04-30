# Module Link 高级特性愿景（按当前实现校准）

本文档聚焦“模块与工程语义”相关能力（`import/export/from/as`、`module/system/component/flow/kits`），并以当前仓库实现为基线说明：

1. 语义链路已做到哪里；
2. 尚未完成的实现缺口是什么；
3. 后续迭代应按什么顺序推进。

## 1. 目标与边界

- **工程级可见性**：`import/export/from/as` 决定跨模块符号可见范围。
- **运行结构承载**：`module/system/component/flow/kits` 定义领域语义到 IR/运行时的映射入口。
- **统一约束**：所有语义最终收敛到统一 IR，再进入 `Verify -> LowerToChunk -> Linker -> Runtime -> VM`。

## 2. 当前实现快照（2026-04 校准）

### 2.1 已落地

- Parser 已支持并产出对应 AST：
  - `ImportDecl` / `ExportDecl` / `ModuleDecl`
  - `SystemDecl` / `ComponentDecl` / `FlowDecl` / `KitsDecl`
- Driver 已有模块可见性链路：
  - `buildModuleSemanticContext`
  - `buildModuleExportTable`
  - `resolveVisibleSymbols`
- TypeChecker 可消费 `visible_symbols` 并参与符号解析一致性校验。
- `component/kits/system/flow` 的声明检查入口已在领域层实现并接入：
  - 分发入口仍在 `src/l0_core/semantic/type_checker_decl.cpp`
  - 具体检查函数位于 `src/l1_domain/analyzer.cpp`
- `buildKitsDecl` / `buildComponentDecl` 已迁入领域层：
  - `src/l1_domain/ir.cpp`

### 2.2 仍在缺口区

- `flow` 的语义检查仍是轻量占位（尚未形成完整规则与上下文约束）。
- `system/component/flow` 尚未形成完整“可执行语义闭环”（从语义到 IR 到运行时调度仍需补齐）。
- `kits` 的窗口权限、逃逸约束、滑动语义目前主要停留在设计与规则定义层，尚未全量代码化。

## 3. Token -> AST 对应关系（契约）

| TokenType | Parser 入口 | AST 落点 |
|---|---|---|
| `KW_IMPORT` | `parseImportDecl()` | `NodeType::ImportDecl` |
| `KW_EXPORT` | `parseExportDecl()` | `NodeType::ExportDecl` |
| `KW_MODULE` | `parseModuleDecl()` | `NodeType::ModuleDecl` |
| `KW_SYSTEM` | `parseSystemDecl()` | `NodeType::SystemDecl` |
| `KW_COMPONENT` | `parseComponentDecl()` | `NodeType::ComponentDecl` |
| `KW_FLOW` | `parseFlowDecl()` | `NodeType::FlowDecl` |
| `KW_KITS` | `parseKitsDecl()` | `NodeType::KitsDecl` |

## 4. 分特性状态与推进建议

### 4.1 `import/export/from/as`

- 目标：导出集合与可见性表一致，`TypeChecker` 与 `Linker` 的行为一致且可诊断。
- 现状：Driver 侧模块导出/可见性框架已就位。
- 下一步：
  1. 继续收紧未导出符号导入的诊断粒度；
  2. 明确同名冲突时的 `as` 重命名策略与错误优先级。

### 4.2 `module`

- 目标：module 成为明确语义边界（导出墙 + 作用域边界）。
- 现状：`ModuleDecl` 已贯穿 parse/driver/语义。
- 下一步：
  1. 细化 module 内 import/export 的作用域规则；
  2. 固化冲突诊断的 span 落点规范。

### 4.3 `system/component/flow`

- 目标：从“声明存在”升级为“可验证、可执行”。
- 现状：
  - 语义入口已迁到 `l1_domain`；
  - `component/kits` IR 构建入口也已在 `l1_domain`；
  - `flow` 仍需补齐完整语义规则。
- 下一步：
  1. 先补 `flow` 与 system 主体约束；
  2. 再补运行期承载（调度模型、入口绑定策略）。

### 4.4 `kits`

- 目标：system 通过窗口访问 component，禁止直连。
- 现状：已形成规则方向与诊断语义，但需继续实装与回归矩阵覆盖。
- 下一步：
  1. 固化 alias 冲突与权限规则；
  2. 落地 escape 检测；
  3. 在 IR/Runtime 层补窗口实例化与生命周期语义。

## 5. 建议执行顺序（落地优先级）

1. 语义层：补齐 `flow` 与 kits 规则闭环。
2. IR 层：补齐 system/flow 运行承载数据结构。
3. Runtime 层：定义 system/flow 触发与调度关系。
4. 诊断层：统一错误码与 span 规范，纳入 `docs/diagnostics/core_error_codes.md`。

## 6. 代码锚点（当前有效）

- Parser：`src/l0_core/syntax/parser_declaration.cpp`
- Driver：`src/driver/driver.cpp`
- TypeChecker 分发：`src/l0_core/semantic/type_checker_decl.cpp`
- 领域语义实现：`src/l1_domain/analyzer.cpp`
- 领域 IR 构建：`src/l1_domain/ir.cpp`
- 统一分层契约：`docs/architecture/layering_ir_contract.md`
