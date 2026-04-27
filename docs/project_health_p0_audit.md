# Niki 项目健康度体检报告（P0）

**文档性质**：工程诊断与优先级建议，与 `MILESTONES.md` 中的 M0～M2 对齐。  
**更新说明**：首版整理自 2026-04 代码审查；含二次深入核对（VM 指令分发、Driver 流水线、语义层与测试矩阵）。

---

## 1. 结论摘要

在引入高级语言特性（异步、借用、插件式扩展等）之前，当前最紧迫的是 **「主路径可信」** 与 **「多文件工程语义闭环」**：

1. **编译器已发射、虚拟机未实现的字节码**会导致「类型过了、能生成 chunk、运行时才炸」——属于最高优先级缺陷类。
2. **语义检查按单文件 AST 进行**，跨文件符号对类型系统不可见；与「项目级 Driver + Linker」组合在一起时，会出现「链接/运行可能成立，但语义层不成立」或相反的不一致，需尽早定策略并收口。
3. **Linker 的字符串池合并与操作数重映射**在头文件中承诺为后续能力，`.cpp` 中仍为占位实现；在依赖「全项目共享 `GlobalInterner`」的前提下可维持 MVP，但必须把**不变量写死并加测试**，否则后续一改链接策略就容易出现隐蔽错误。
4. **回归测试**当前未覆盖 `TypeChecker` / 端到端 Driver 项目；仅靠单元测试与手工脚本时，上述问题容易反复回归。

---

## 2. P0 问题总表

| ID | 领域 | 严重程度 | 简述 |
|----|------|----------|------|
| P0-1 | VM / Compiler | **阻断级** | 字符串/对象相等比较等 opcode 已发射，VM 统一走「未实现」分支 |
| P0-2 | Semantic / Driver | **阻断级** | 每文件独立 `TypeChecker::check`，缺少项目级符号环境，跨文件调用语义与工程模型不一致 |
| P0-3 | Linker | **高风险** | `mergeStringPools` / `remapChunkOperands` 等为 stub；依赖共享 interner 的隐含契约未用测试锁死 |
| P0-4 | 语言表面 | **高** | Parser/AST 覆盖面大于 Compiler 已实现子集；大量声明/语句路径为明确「未实现」报错 |
| P0-5 | 工程基线 | **高** | `MILESTONES.md` M0：构建、`ctest`、`scripts/test.nk` 端到端需保持可重复绿基线 |
| P0-6 | 测试矩阵 | **中高** | CMake 未注册语义/类型检查专项测试；无 Driver 级集成测试 |
| P0-7 | 语义实现细节 | **中** | `Unknown` 操作数在比较运算上被宽松推断为 `Bool`，可能掩盖错误并仍向下编译 |
| P0-8 | 产品一致性 | **低～中** | Launcher 错误文案硬编码「main」，与 `DriverOptions::entry_name` 可配置性略不一致 |
| P0-9 | 文档与实现 | **低** | 虚拟机设计文档强调 Scanner 按需迭代；Driver 当前将全文件 token 化入 `vector`（实现选择，非功能错误，但与文档叙事不一致） |

---

## 3. P0-1：Opcode 对齐（Compiler ↔ VM）

### 现象

`compiler_expression.cpp` 在 `==` / `!=` 上对 `String` 发射 `OP_SEQ` / `OP_SNE`，对其它非数值类型发射 `OP_OEQ` / `OP_ONE`。

`vm.cpp` 中上述 opcode 与 `OP_INVOKE`、`OP_GET_PROPERTY`、`OP_SET_PROPERTY`、`OP_METHOD`、`OP_THROW`、`OP_CATCH` 共处于同一分支，统一 `runtime_error("Opcode not implemented yet.")`。

### 影响

- 合法类型检查下的**字符串比较**等路径可能在运行期直接失败。
- 与 `MILESTONES.md` M1 中「编译器与 VM opcode 对齐表」「未支持特性明确报错而非静默失败」直接冲突。

### 建议方向（二选一或组合）

- **实现**：在 VM 中补齐与 `opcode.hpp` 及 Compiler 发射策略一致的语义。
- **收紧**：在语义或编译阶段禁止生成尚未实现的 opcode，并给出稳定 `Diagnostic.code`（避免「能编不能跑」）。

---

## 4. P0-2：跨文件语义（项目级类型检查）

### 现象

`driver.cpp` 中每个源文件独立执行：`Scanner` → `Parser` → `TypeChecker::check` → `Compiler::compile`，仅在 `compileAll` 层共享 `GlobalInterner`。

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

`linker.cpp` 末尾 `mergeStringPools`、`remapChunkOperands`、`resolveSymbols`、`mergeInitChunks` 为 MVP 占位；主流程 `link` 已包含：

- 多模块 `init_chunk` 顺序保留；
- 合并后的 `program.string_pool`（用于诊断等）；
- 基于常量池扫描的重复符号与入口决议。

### 隐含契约（需在文档与测试中写死）

当前链接器依赖：**跨模块 `name_id` 由共享 `GlobalInterner` 统一**，从而弱化「每模块 chunk 内 string 池索引重映射」的紧迫性。一旦未来出现：

- 每模块独立池与索引，或  
- 链接期合并常量池，

则占位接口必须落地，否则易出现「静默错误执行」类缺陷（`MILESTONES.md` M2 风险栏已提及）。

---

## 6. P0-4：语言表面 vs 可实现子集

### 现象（节选）

以下路径在 Compiler 中明确报「未实现」（grep 可复现），包括但不限于：

- **声明类**：`Interface` / `Enum` / `TypeAlias` / `Impl` / `System` / `Component` / `Flow` / `Kits` / `Tag` / `TagGroup` 等（`compiler_declaration.cpp`）。
- **语句类**：`Match` / `Nock` / `Attach` / `Detach` / `Target` 等（`compiler_statement.cpp`）。
- **表达式类**：部分动态属性、`Dispatch`、`Await`、`Borrow`、隐式转换等（`compiler_expression.cpp`）。

### 影响

Parser 越完整，用户越容易误以为特性已可用；与 M1「V0 最小闭环」目标叠加时，需要 **白名单文档** 或 **更早阶段的拒绝与诊断**，降低无效试错成本。

---

## 7. P0-5：工程基线（M0）

以 `MILESTONES.md` M0 验收为准：

- `cmake --preset default` 成功  
- `cmake --build --preset default` 成功  
- `ctest --test-dir build` 全绿  
- `scripts/test.nk` 完成一次端到端执行  

**说明**：上述为发布与协作的最低门槛；任何 P0-1～P0-4 的修复都应在该基线绿的前提下合并。

---

## 8. 深入考察：测试矩阵（P0-6）

### 当前 `CMakeLists.txt` 中 `niki_tests` 组成

- `scanner_test` / `parser_test` / `compiler_test` / `linker_test` / `launcher_test`

### 缺口

- **无** `type_checker_*` 或语义层专项测试 → `Unknown` 恢复、比较运算宽松分支等易回归且难察觉。
- **无** `Driver::runProject` 或「收集 → 编译全部 → 链接 → 启动」的集成测试 → 多文件与诊断聚合路径依赖 `scripts/cases` 与手工。

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

`driver.cpp` 中 `compileOneModule` 当前将 `scanToken()` 结果循环 `push_back` 至 `std::vector<syntax::Token>`，即**全文件物化 Token 列表**。

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
| 诊断规范 | `docs/diagnostic_conventions.md` |
| Opcode 枚举 | `include/niki/vm/opcode.hpp` |
| VM 分发 | `src/vm/vm.cpp` |
| 比较表达式发射 | `src/syntax/compiler_expression.cpp` |
| Driver 流水线 | `src/driver/driver.cpp` |
| Linker MVP 与占位 | `src/linker/linker.cpp` |
| 语义二元表达式 | `src/semantic/type_checker_expr.cpp` |
| 启动与入口 | `src/runtime/launcher.cpp` |
| 测试注册 | `CMakeLists.txt` |
| 跨文件语义用例（脚本） | `scripts/cases/fail/semantic_01_cross_file_call/` |

---

*本报告由代码审查生成；若仓库后续提交修改了上述路径行为，请以实际代码为准并更新本节对应描述。*
