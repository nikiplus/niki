# Niki 项目健康度体检报告（P0）

**文档性质**：工程诊断与优先级建议，与 `MILESTONES.md` 中的 M0～M2 对齐。  
**更新说明**：首版整理自 2026-04 代码审查；2026-04-30 复核路径/测试清单；**2026-05-18 M0 工程基线已完成**（见 [docs/engineering/m0_baseline.md](../engineering/m0_baseline.md)）。  
**迁移注记**：文中部分条目基于旧 `Compiler` / `driver` 路径描述；当前主链路为 `meta::orchestrator` + `IRBuilder -> Verify -> LowerToChunk`。

---

## 1. 结论摘要

在引入高级语言特性（异步、借用、插件式扩展等）之前，当前最紧迫的是 **「主路径可信」** 与 **「多文件工程语义闭环」**：

1. **IR 降级路径可生成、虚拟机未实现的字节码**会导致「类型过了、能生成 chunk、运行时才炸」——属于最高优先级缺陷类。
2. **语义检查按单文件 AST 进行**，跨文件符号对类型系统不可见；与「项目级 Driver + Linker」组合在一起时，会出现「链接/运行可能成立，但语义层不成立」或相反的不一致，需尽早定策略并收口。
3. **Linker 的字符串池合并与操作数重映射**在头文件中承诺为后续能力，`.cpp` 中仍为占位实现；在依赖「全项目共享 `StringInterner`」的前提下可维持 MVP，但必须把**不变量写死并加测试**，否则后续一改链接策略就容易出现隐蔽错误。
4. **回归测试**对语义专项与复杂工程场景覆盖仍偏薄；仅靠当前单元测试与手工脚本时，上述问题仍可能回归。

### 2026-05-18 现状快照（M0 后）

- **M0 已完成**：`CMakePresets.json`、204 项 `ctest` 全绿、`scripts/smoke` 端到端、GitHub Actions CI、工程文档（README / m0_baseline / build_and_test）。
- L0/L1 分层改造主干已落地；`buildModuleSemanticContext` 已接入跨文件可见性（P0-2 部分缓解，规则仍待收紧）。
- Opcode 能力表 + `writeRunnableOp` 已阻断多数「能编不能跑」路径（P0-1 部分缓解）。
- 代码仍存在 P0-1～P0-4、P0-7 等语义/领域风险；**下一优先级为 M1 主路径可信**，而非重复 M0 工程项。

---

## 2. P0 问题总表

| ID | 领域 | 严重程度 | 简述 |
|----|------|----------|------|
| P0-1 | VM / IR Lowering | **阻断级** | 部分 opcode 可由 IR lowering 产出，但 VM 仍走「未实现」分支 |
| P0-2 | Semantic / Precompile-Orchestrator | **阻断级** | 跨文件语义仍依赖显式模块语义上下文构建，若规则收口不足易导致语义与工程模型不一致 |
| P0-3 | Linker | **高风险** | `mergeStringPools` / `remapChunkOperands` 等为 stub；依赖共享 interner 的隐含契约未用测试锁死 |
| P0-4 | 语言表面 | **高** | 领域声明已接入 `l1_domain`，但 `system/flow/kits` 的语义到可执行链路仍未完全闭环 |
| P0-5 | 工程基线 | ~~**高**~~ **已收口（M0）** | Preset、ctest 139 项、smoke、CI；见 [m0_baseline.md](../engineering/m0_baseline.md) |
| P0-6 | 测试矩阵 | **中** | 已注册 `type_checker_*`、`diagnostic_test`、10×cases、`smoke_e2e`；仍缺 `runProject` 目录级集成测试 |
| P0-7 | 语义实现细节 | **中** | `Unknown` 操作数在比较运算上被宽松推断为 `Bool`，可能掩盖错误并仍向下编译 |
| P0-8 | 产品一致性 | **低** | Launcher 已使用通用诊断文案；入口名由 `LinkOptions::entry_name` 参与链接阶段 |
| P0-9 | 文档与实现 | **低** | 虚拟机设计文档强调 Scanner 按需迭代；`parse_stage` 当前将全文件 token 化入 `vector`（实现选择，非功能错误） |

---

## 3. P0-1：Opcode 对齐（IR Lowering ↔ VM）

### 现象

`lower_to_chunk` 在 IR 到字节码映射中会生成一组 VM opcode；其中部分 opcode 在 VM 侧仍处于未完整实现状态。

`vm.cpp` 中上述 opcode 与 `OP_INVOKE`、`OP_GET_PROPERTY`、`OP_SET_PROPERTY`、`OP_METHOD`、`OP_THROW`、`OP_CATCH` 共处于同一分支，统一 `runtime_error("Opcode not implemented yet.")`。

### 影响

- 合法类型检查下的**字符串比较**等路径可能在运行期直接失败。
- 与 `MILESTONES.md` M1 中「前后端 opcode 对齐表」「未支持特性明确报错而非静默失败」直接冲突。

### 建议方向（二选一或组合）

- **实现**：在 VM 中补齐与 `opcode.hpp` 及 IR lowering 发射策略一致的语义。
- **收紧**：在语义或编译阶段禁止生成尚未实现的 opcode，并给出稳定 `Diagnostic.code`（避免「能编不能跑」）。

---

## 4. P0-2：跨文件语义（项目级类型检查）

### 现象

`meta/orchestrator` 编排层中每个源文件独立执行：`Scanner` → `Parser` → `TypeChecker::check` → `IRBuilder` → `Verify` → `LowerToChunk`，仅在 `compileAll` 层共享 `StringInterner`。

因此**文件 B 无法在类型检查阶段看到文件 A 的顶层符号**，除非未来引入：

- 项目级预扫描 / 合并符号表，或  
- 单池 AST 的多根检查，或  
- 显式 `import` 与模块图驱动的符号解析。

### 与仓库内证据的对应

`scripts/cases/fail/semantic_01_cross_file_call/` 用例描述「跨文件调用」类场景；当前架构下该类问题属于**设计层已知缺口**，应在 M2「多文件项目」验收中显式列为必须通过或必须明确拒绝（并诊断）。

### 影响

在补齐 `import` / 库管理之前，**「多文件 + 共享 interner + 链接器」**与 **「单文件语义」** 长期并存会导致维护者与用户心智分裂：同一项目，链接与运行期行为与类型系统保证不对齐。

---

## 5. P0-3：Linker 占位与隐含契约

### 现象

链接策略主实现位于 `src/l0_core/linker/linker_facade.cpp`。仍需关注字符串池重映射与深层重定位演进：

- 多模块 `init_chunk` 顺序保留；
- 合并后的 `program.string_pool`（用于诊断等）；
- 基于常量池扫描的重复符号与入口决议。

### 隐含契约（需在文档与测试中写死）

当前链接器依赖：**跨模块 `name_id` 由共享 `StringInterner` 统一**，从而弱化「每模块 chunk 内 string 池索引重映射」的紧迫性。一旦未来出现：

- 每模块独立池与索引，或  
- 链接期合并常量池，

则占位接口必须落地，否则易出现「静默错误执行」类缺陷（`MILESTONES.md` M2 风险栏已提及）。

---

## 6. P0-4：语言表面 vs 可实现子集（IRBuilder）

### 现象（节选）

以下路径在当前主链路中仍需持续收口，包括但不限于：

- **声明类**：`Interface` / `Enum` / `TypeAlias` / `Impl` / `System` / `Flow` / `Kits` / `Tag` / `TagGroup` 等。
- **语句类**：`Match` / `Nock` / `Attach` / `Detach` / `Target` 等。
- **表达式类**：部分动态属性、`Dispatch`、`Await`、`Borrow`、隐式转换等。

### 影响

Parser 越完整，用户越容易误以为特性已可用；与 M1「V0 最小闭环」目标叠加时，需要 **白名单文档** 或 **更早阶段的拒绝与诊断**，降低无效试错成本。

---

## 7. P0-5：工程基线（M0）— 已完成

M0 已于 2026-05-18 完成，权威操作说明见 [docs/engineering/m0_baseline.md](../engineering/m0_baseline.md)。

验收摘要：

- [x] `cmake --preset default` / `cmake --build --preset default`
- [x] `ctest --test-dir build` 全绿（139 项）
- [x] `.\build\NIKI.exe scripts\smoke` 退出码 0
- [x] GitHub Actions CI

**说明**：后续 P0-1～P0-4 修复须在绿基线上合并；新增回归优先接入 CTest（`run_case.py` 或 GTest）。

---

## 8. 深入考察：测试矩阵（P0-6）

### 当前测试组成（2026-05-18）

**GTest（`niki_tests`）**：含 `scanner_test`、`parser_test`、`parser_stmt_test`、`type_checker_test`、`type_checker_stmt_test`、`module_chain_test`、`builder_test`、`verify_test`、`module_ir_test`、`lower_to_chunk_test`、`driver_project_test`、`linker_test`、`launcher_test`、`endpoint_test`、`domain_split_test`、`diagnostic_test`。

**CTest 附加**：`layer_boundaries`、`smoke_e2e`、10×`cases_*`（见 m0_baseline.md）。

### 缺口

- **无** `CompilerOrchestrator::runProject` 临时目录级集成测试（cases 仅覆盖固定目录）。
- `Unknown` 恢复等语义边角仍易回归，需 M1 补专用断言。

### 建议（非必须一次做完）

- 为 `TypeChecker` 增加最小 GTest 集（与 `diagnostic::codeOf(events::SemanticCode::...)` 语义错误码断言配合更佳）。
- 增加 1～2 个「临时目录 + 多 `.nk`」的 Driver 集成测试，覆盖成功与可预期失败（与 `scripts/cases` 对齐）。

---

## 9. 深入考察：语义宽松路径（P0-7）

### 位置

`type_checker_expr.cpp` 中 `checkBinaryExpr`：若左右任一为 `Unknown`，且运算符为比较类，则直接返回 `Bool` 类型。

### 风险

若上游标识符解析已报错但类型流仍携带 `Unknown`，比较表达式可能被赋予 `Bool`，后续仍可能生成字节码，表现为**错误被部分掩盖**或诊断顺序不符合直觉。与 M1「明确报错」目标存在张力，建议在 M1 收口阶段复查该恢复策略。

### 备注

`checkIdentifierExpr` 内注释称「未找到则返回 Unknown 而不硬失败」，与 `resolveSymbol` 当前实现（未找到即 `reportError`）可能不一致；属**注释陈旧或历史分支残留**，建议单独清理以免误导后续贡献者。

---

## 10. 深入考察：Launcher 文案与配置（P0-8）

`launcher.cpp` 在入口查找失败时使用固定中文说明「未找到入口函数**main**」，而 `DriverOptions` 支持配置 `entry_name`。行为上仍以 `program.entry_name_id` 为准，但**用户可见错误信息**可能与配置不一致，建议在后续小改动中改为使用 `options` 或 `LinkOptions` 中的入口名参与拼接。

---

## 11. 深入考察：Scanner 策略与文档叙事（P0-9）

`NIKI 虚拟机设计指北.md` 强调 Scanner 按需、迭代器式、避免整文件 Token 向量。

`meta/precompile/parse_stage.cpp` 当前将 `scanToken()` 结果循环 `push_back` 至 `std::vector<syntax::Token>`，即**全文件物化 Token 列表**。

**结论**：非功能错误，属于实现与长期文档目标的差异；若在性能或内存上遇到瓶颈，可再评估改为 Parser 驱动的按需扫描。

---

## 12. 诊断码与语义阶段

当前语义阶段错误码仍偏泛化；若长期共用同一 `code`，不利于机器侧分类统计。属 **P1 体验/生态** 项，可在 P0 收口后再扩展细分事件码表。

---

## 13. 建议的后续工作顺序（执行层）

1. **关闭 P0-1**：VM 实现或编译期禁止（与 `opcode.hpp` 单一真相对齐）。  
2. **决策并实施 P0-2**：跨文件语义「支持」或「显式拒绝 + 诊断」，与 `scripts/cases` 对齐。  
3. **文档化或测试化 P0-3**：共享 interner 不变量 + 链接器演进接口的归属说明。  
4. **维持 P0-5 绿基线**，并逐步补齐 **P0-6**。  
5. **M1 阶段**复查 **P0-7**、修正 **P0-8**、视需要处理 **P0-9**。

---

## 14. 参考文件索引

| 主题 | 路径 |
|------|------|
| 里程碑与验收 | `MILESTONES.md` |
| 诊断规范 | `docs/diagnostics/diagnostic_conventions.md` |
| 模块语义愿景 | `docs/architecture/module_link_feature_vision.md` |
| Opcode 枚举 | `include/niki/l0_core/vm/opcode.hpp` |
| VM 分发 | `src/l0_core/vm/vm.cpp` |
| IR 降级实现 | `src/l0_core/ir/lower_to_chunk.cpp` |
| 预编译阶段实现 | `src/meta/precompile/parse_stage.cpp` / `src/meta/precompile/predeclare_stage.cpp` / `src/meta/precompile/module_context_stage.cpp` |
| 编排层入口 | `src/meta/orchestrator/compiler_orchestrator.cpp` |
| Linker 策略实现 | `src/l0_core/linker/linker_facade.cpp` |
| 语义二元表达式 | `src/l0_core/semantic/type_checker_expr.cpp` |
| 启动与入口策略实现 | `src/l0_core/runtime/launcher.cpp` |
| 测试注册 | `CMakeLists.txt` |
| 跨文件语义用例（脚本） | `scripts/cases/fail/semantic_01_cross_file_call/` |

---

*本报告由代码审查生成；若仓库后续提交修改了上述路径行为，请以实际代码为准并更新本节对应描述。*

## Legacy 备注（旧 Compiler 路径）

- 历史上项目存在 `syntax/compiler*` 路径；该路径已从主链路与构建中移除。
- 本文若仍出现 `Compiler` 术语，均仅表示历史背景，不代表当前实现入口。
