# 重构边界残余检查报告（meta向）

日期：2026-05-01  
范围：`linker`、`compile_pipeline`、`compile_orchestrator`、`parse_stage`、`predeclare_stage`、`module_context`

## 1. 背景与目标

本次检查针对“核心层部分功能上移至 lv1 领域层、另一部分下沉至 meta 层”后的边界残余。  
重点不是功能正确性回归，而是职责归属是否仍有交叉、重复与泄漏。

---

## 2. 总体结论

当前代码已经形成“`meta::orchestrator` 负责流程编排、`meta::precompile` 负责语义前置、`l0::linker` 门面 + `meta::project_linker` 标准实现负责链接策略”的骨架，但仍存在以下典型残余：

1. **（linker 已收敛）`l0` 门面仍转发至 `meta` 实现**：边界已明确为主动设计而非重复实现分歧；演进见 [Linker 演进方案](../architecture/linker_evolution_plan.md)。  
2. **`compile_pipeline` 与 `predeclare_stage` 重复维护顶层声明收集逻辑**，存在分叉风险。  
3. **`compile_orchestrator` 仍直接编排部分 `l0` 细节**（`TypeChecker`、`VM/Launcher`），边界仍偏“重”。  
4. **`module_context_stage` 的模块名绑定策略依赖文件名 stem + interner 查找**，与“语义模块名”存在潜在错位。  
5. **`parse_stage` / `predeclare_stage` 产物契约未以独立 stage context 显式化**，流水线组合仍较“隐式耦合”。

---

## 3. 分模块检查结果

## 3.1 linker（`l0_core/linker` + `meta/project_linker`）

### 改造状态（2026-05-01 已完成）
- 已将 `meta::project::ProjectLinker` 固化为链接阶段唯一标准实现。
- `l0_core/linker::Linker` 明确为 Facade，只保留稳定 `link()` 入口。
- 已删除 `l0_core/linker` 中未启用的影子私有接口（`mergeStringPools/remapChunkOperands/resolveSymbols/mergeInitChunks`）。

### 现状
- `l0_core/linker::Linker::link` 当前直接委托到 `meta::project::ProjectLinker::link`。
- 真实链接策略（入口决议、重复符号检查、字符串池合并）位于 `src/meta/project/project_linker.cpp`。
- `include/niki/l0_core/linker/linker_facade.hpp` 当前承载链接阶段对外契约（`CompileModule`/`LinkedProgram`/`LinkOptions`）与门面 `Linker`。

### 边界残余
- **已收敛主残余**：双实现分叉风险已解除，当前不存在 l0 内部影子实现。
- 当前是“l0 稳定入口 + meta 唯一实现”的分层形态，需在架构文档中持续强调该约束，避免未来回流。

### 风险
- 后续新增链接特性（重定位/重映射）如果直接加回 `l0::Linker`，可能重新引入双实现。
- 若调用方绕过 Facade 直接散落依赖 `meta/project_linker`，会削弱入口稳定性。

### 建议
- 新增链接能力统一落在 `meta::project::ProjectLinker`，`l0::Linker` 仅做门面转发。
- 为 linker 增补一条约束测试：验证 `l0::Linker::link` 与 `ProjectLinker::link` 行为一致（同输入同诊断/产物）。
- **远期演进路线**：见 [Linker 演进方案](../architecture/linker_evolution_plan.md)（现状不变量、池/remap、落盘 object、增量链接分阶段）。

---

## 3.2 compile_pipeline（`meta/orchestrator/compile_pipeline`）

### 现状
- 负责单 unit 的 IR build/verify/lower，并封装为 `CompileModule`。
- `compileUnitChunk` 参数包含 `GlobalTypeArena/GlobalSymbolTable`，当前实现中显式 `(void)` 未使用。
- 文件内定义了 `collectTopLevelDecls`（用于 verify issue 位置映射辅助）。

### 边界残余
- **接口保留了语义上下文参数，但阶段内部不消费**，反映旧边界迁移未收敛。
- `collectTopLevelDecls` 与 `meta::precompile::collectTopLevelDecls` 形成重复实现，职责归属不清。

### 风险
- 后续 parser/AST 结构调整时，两处顶层收集逻辑容易不一致，造成报错定位漂移。
- 误导调用方认为该阶段依赖全局语义状态。

### 建议
- 将 `compileUnitChunk/compileParsedUnit` 参数收敛为仅保留实际依赖，或通过 `CompileBackendContext` 显式声明“可选但当前未用”字段。
- 删除本地 `collectTopLevelDecls`，统一复用 `meta::precompile::collectTopLevelDecls`（或抽到共享 util）。

---

## 3.3 compile_orchestrator（`meta/orchestrator/compiler_orchestrator`）

### 现状
- 已承担项目级流程：文件收集 -> parse -> predeclare -> module context -> typecheck -> backend compile -> link -> run。
- 调用 `meta::precompile` 和 `meta::orchestrator::compile_pipeline` 完成前后端阶段。
- 仍直接 new/use `semantic::TypeChecker`、`vm::VM`、`runtime::Launcher`。

### 边界残余
- **编排层仍接触底层执行细节**（typecheck 实例化、vm 启动），属于“编排层偏重”残余。
- `compileAll` 内部串联了过多阶段细节，缺少 stage 对象化/策略注入点。

### 风险
- 难以做“仅编译不运行”“仅产物导出”“替换执行后端”等场景扩展。
- orchestrator 单文件复杂度持续上升，成为新的耦合中心。

### 建议
- 把 `typecheck + backend compile` 合并为独立 `semantic_compile_stage`（输入 `ModuleSemanticContext`，输出 `CompileModule[]`）。
- 把 `link + run` 提取为 `execution_stage`，orchestrator 仅保留组装与错误收口。

---

## 3.4 parse_stage（`meta/precompile/parse_stage`）

### 现状
- 职责清晰：scan + parse，填充 `GlobalCompilationUnit` 的 `tokens/root`。
- 无符号解析、无可见性构建，单一职责执行较好。

### 边界残余
- `parseIntoCompilationUnit` 直接读写 `unit.pool.source_path`、`unit.tokens`、`unit.root`，采用“共享可变对象协议”。
- 与后续 `predeclare/module_context` 的阶段契约未通过显式 context 类型约束。

### 风险
- 阶段前后依赖点隐式，后续并行化或缓存化改造困难。

### 建议
- 定义 `ParsedUnit`（不可变或只读视图）作为 parse 输出，再由 orchestrator 组装回全局 unit。
- 至少在 `precompile_pipeline.hpp` 增加阶段输入输出契约注释（必填字段、副作用字段）。

---

## 3.5 predeclare_stage（`meta/precompile/predeclare_stage`）

### 现状
- 负责顶层 function/struct/typealias 的预声明，写入 `GlobalSymbolTable/GlobalTypeArena`。
- 能处理 `export wrapped decl` 的预声明映射。

### 边界残余
- 存在重复逻辑块：普通 `FunctionDecl/StructDecl/TypeAliasDecl` 与 `ExportDecl(wrapped)` 分支基本重复。
- `collectTopLevelDecls` 的“outer ModuleDecl/primary module”策略属于结构归一化逻辑，当前仅在该文件维护。

### 风险
- 新增声明种类时需在多处分支同步更新，容易漏改。
- 逻辑复用不足导致错误信息一致性难保证。

### 建议
- 提取 `predeclareFunction/predeclareStruct/predeclareTypeAlias` 三个局部 helper，`ExportDecl` 仅做解包后复用。
- 将“模块顶层归一化策略”抽到共享组件，避免其他阶段自行再解释 AST 顶层。

---

## 3.6 module_context（`meta/precompile/module_context_stage`）

### 现状
- 构建 `ModuleRegistry`、`ModuleExportTable`、`UnitVisibleSymbols`，为 typecheck 提供可见符号。
- import 解析使用 `module_name_id -> module_id` 映射。

### 边界残余
- 映射建立依赖 `source_path` 的文件名 stem 与 interner 反查；不是基于显式 `ModuleDecl` 语义名。
- `import module only` 目前直接跳过，不形成显式模块可见实体（语义策略未完全收敛）。

### 风险
- 文件重命名与模块声明名不一致时，import 解析可能出现语义偏差。
- 后续支持 `import module as alias` 等语法时需要返工当前映射策略。

### 建议
- 以 `ModuleDecl` 的显式模块名作为 registry 主键，文件 stem 仅作为回退策略。
- 为 `import module only` 设计明确语义：是否导入模块命名空间对象，或显式标注“仅触发副作用加载”。

---

## 4. 优先级收敛清单

### P0（建议本轮立即收敛）
1. 统一 linker 实现归属（`l0` 或 `meta` 二选一，移除另一侧“壳/影子接口”）。  
2. 消除 `collectTopLevelDecls` 重复实现，形成单一来源。  
3. 明确 `compile_pipeline` 参数契约，去掉未使用全局语义参数或显式上下文化。

### P1（建议下一轮）
1. `compiler_orchestrator` 进一步瘦身：拆分 `semantic_compile_stage` 和 `execution_stage`。  
2. `module_context_stage` 切换到“显式模块名优先”的 registry 策略。  
3. `predeclare_stage` 去重，统一 wrapped/normal 声明处理路径。

### P2（持续演进）
1. 为 precompile 各 stage 引入显式输入输出 context 类型，降低隐式共享状态。  
2. 补齐按层次划分的测试（stage 单测 + orchestrator 集成测）。

---

## 5. 建议的验收标准（DoD）

- 同一类能力只有一个“真实实现层”，其他层只做门面或不暴露。  
- 关键 AST 解释逻辑（如顶层声明收集）在代码库只有一个 authoritative 实现。  
- orchestrator 文件中不再出现可独立成阶段的底层细节实现。  
- 模块名解析对“文件名变化”不敏感，以语义模块名为准。  
- 文档与测试均能清楚回答“某能力属于哪一层”。

