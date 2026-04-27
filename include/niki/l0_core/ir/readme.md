# IR 重构执行方案（激进版）

本文档用于指导 Niki 在 `l0_core` 层从“AST 直出 Chunk”迁移到“IR-first”编译流水线。

## 1. 目标与范围

### 1.1 总目标

将当前主链路：

- `AST + node_types -> Chunk -> Linker -> Runtime`

迁移为：

- `AST + node_types -> ModuleIR -> Lowering -> Chunk -> Linker -> Runtime`

### 1.2 迁移原则

- 主路径优先：先打通可编译、可链接、可运行，再做优化。
- 显式契约：符号、导入导出、入口决议都以显式结构表达，不依赖常量池“推断”。
- 阶段可回退：每个阶段独立可构建、可测试，避免一次性大爆炸。
- 诊断稳定：未实现特性必须给出明确诊断，不允许静默降级。

## 2. 现状问题（重构驱动）

- `syntax::Compiler` 与 VM opcode 发射紧耦合，前后端职责混杂。
- `linker` 当前存在 MVP 占位接口，符号决议仍有隐式假设。
- 多文件语义（可见性/导出）与链接阶段存在潜在心智分裂风险。
- 高级能力（模块化、优化、自举）需要中间层承接。

## 3. 最终架构（目标态）

- `syntax`：Scanner/Parser（前端构建 AST）
- `semantic`：TypeCheck + 模块可见性约束
- `ir`：
  - `builder`：AST -> ModuleIR
  - `verify`：IR 结构与约束校验
  - `lower_to_chunk`：IR -> VM Chunk
- `linker`：消费 `CompileModule` 的显式符号/导入导出信息完成链接
- `runtime/vm`：执行链接产物

## 4. 分阶段实施计划

> 建议分支：`refactor/ir-first`  
> 每阶段必须通过构建与最小回归后再进入下一阶段。

### 阶段 0：基线固化（半天）

#### 目标

- 固定当前可运行基线，便于回归对比。

#### 涉及文件（只读核对）

- `CMakeLists.txt`
- `src/driver/driver.cpp`
- `src/l0_core/syntax/compiler.cpp`
- `src/l0_core/linker/linker.cpp`
- `MILESTONES.md`

#### 验收

- `cmake --build --preset default` 成功
- `ctest --test-dir build` 达到当前基线

---

### 阶段 1：IR 模块骨架落地（1~2 天）

#### 目标

- 新增 IR 数据结构与校验器，不改变现有行为。

#### 新增/修改文件

- 新增：
  - `include/niki/l0_core/ir/module_ir.hpp`
  - `include/niki/l0_core/ir/builder.hpp`
  - `include/niki/l0_core/ir/verify.hpp`
  - `include/niki/l0_core/ir/lower_to_chunk.hpp`
  - `src/l0_core/ir/module_ir.cpp`
  - `src/l0_core/ir/verify.cpp`
  - `src/l0_core/ir/lower_to_chunk.cpp`
  - `test/ir/ir_verify_test.cpp`
- 修改：
  - `CMakeLists.txt`（接入 IR 源码与测试）

#### 必备数据结构（`module_ir.hpp`）

- `IRType`
- `IRValue`（vreg/immediate/symbol）
- `IRInstKind`
- `IRInst`
- `IRBasicBlock`
- `IRFunction`
- `ModuleIR`

#### 必备校验规则（`verify`）

- 基本块必须以 terminator 结束（`jump/branch/return`）
- 指令引用寄存器必须合法
- 函数/符号定义不可重复

#### 验收

- 不改变旧流水线行为
- IR 模块可编译，`ir_verify_test` 通过

---

### 阶段 2：Compiler 双阶段接口改造（2 天）

#### 目标

- 将编译器接口拆分为构建 IR 和降级 Chunk 两段。

#### 涉及文件

- `include/niki/l0_core/syntax/compiler.hpp`
- `src/l0_core/syntax/compiler.cpp`
- 可选拆分：
  - `src/l0_core/syntax/compiler_to_ir.cpp`
  - `src/l0_core/syntax/compiler_legacy_codegen.cpp`

#### 变更点

- 新增接口：
  - `buildIR(...) -> expected<ir::ModuleIR, DiagnosticBag>`
  - `lowerIR(...) -> expected<Chunk, DiagnosticBag>`
- `compile(...)` 暂时作为包装器调用上述两段
- 保留临时逃生开关（legacy codegen fallback）

#### 验收

- `compiler_test` 不退化
- 至少一个核心样例可走 IR 路径成功产出 Chunk

---

### 阶段 3：实现最小 IRBuilder（3~4 天）

#### 目标

- 支持 M1 核心语法子集的 AST -> IR。

#### 涉及文件

- `src/l0_core/ir/ir_builder.cpp`（或对应实现文件）
- 参考迁移来源：
  - `src/l0_core/syntax/compiler_expression.cpp`
  - `src/l0_core/syntax/compiler_statement.cpp`
  - `src/l0_core/syntax/compiler_declaration.cpp`

#### 首批支持范围

- 表达式：literal/identifier/binary/compare/logical/call
- 语句：`var/const/assignment/if/loop/return`

#### 非支持路径策略

- 明确诊断并返回失败（禁止静默忽略）

#### 验收

- 核心样例行为与旧路径一致
- 未支持节点有稳定诊断码与错误文案

---

### 阶段 4：Lowering（IR -> Chunk）实现（3 天）

#### 目标

- 将 IR 指令稳定降级到 VM 字节码。

#### 涉及文件

- `include/niki/l0_core/ir/lower_to_chunk.hpp`
- `src/l0_core/ir/lower_to_chunk.cpp`
- 参考：
  - `include/niki/l0_core/vm/opcode.hpp`
  - `src/l0_core/syntax/compiler.cpp`

#### 关键实现点

- IR 指令到 opcode 映射
- 虚拟寄存器到 VM 寄存器分配
- 常量池写入与宽窄常量加载指令选择
- 跳转/分支补丁

#### 验收

- 生成的 Chunk 可被 VM 正常执行
- 回归样例与旧路径结果一致

---

### 阶段 5：Driver 切换 IR 主线（2 天）

#### 目标

- 在项目编排层将编译主路径切到 IR。

#### 涉及文件

- `include/niki/driver/driver.hpp`
- `src/driver/driver.cpp`

#### 变更点

- `typeCheckUnit -> buildIR -> lowerToChunk -> buildCompileModule`
- 编译产物中附加显式符号元信息（供 linker 使用）

#### 验收

- `runProject` 在 IR 路径可跑通多文件样例
- 诊断聚合行为保持稳定

---

### 阶段 6：Linker 显式符号化（2~3 天）

#### 目标

- 不再通过扫描 `Chunk.constants` 推导符号。

#### 涉及文件

- `include/niki/l0_core/linker/linker.hpp`
- `src/l0_core/linker/linker.cpp`
- `test/linker/linker_test.cpp`

#### 变更点

- 扩展 `CompileModule`：
  - `defined_symbols`
  - `imports`
  - `entry_candidates`（可选）
- `Linker::link` 直接用显式符号做：
  - 重复符号检测
  - 入口决议
  - 导入解析

#### 验收

- 重复符号/多入口/入口缺失测试稳定
- 链接行为不依赖常量池布局

---

### 阶段 7：语义闭环（模块可见性）（3~5 天）

#### 目标

- 统一语义层与链接层的模块边界与可见性。

#### 涉及文件

- `src/driver/driver.cpp`
  - `buildModuleSemanticContext(...)`
  - `collectModuleRegistry(...)`
  - `buildModuleExportTable(...)`
  - `resolveVisibleSymbols(...)`
- `include/niki/l0_core/semantic/type_checker.hpp`
- `src/l0_core/semantic/type_checker*.cpp`
- 新增/扩展测试：
  - `test/semantic/module_visibility_test.cpp`
  - `test/driver/driver_project_test.cpp`

#### 验收

- 跨文件可见性与链接结果一致
- 不再出现“语义与链接结论不一致”情况

---

### 阶段 8：删除旧路径与文档收尾（1~2 天）

#### 目标

- 完成 IR-first 收口，去除 legacy 双轨负担。

#### 涉及文件

- 删除/清理 legacy codegen 相关文件
- 更新文档：
  - `include/niki/l0_core/readme.md`
  - `include/niki/l0_core/syntax/readme.md`
  - `include/niki/l0_core/ir/readme.md`（本文）

#### 验收

- 主路径仅保留 IR-first
- 全量测试通过
- 数据流文档与实现一致

## 5. 每阶段固定检查清单

- 构建：`cmake --build --preset default`
- 测试：`ctest --test-dir build`
- 端到端：至少 1 个成功样例 + 1 个失败样例
- 诊断：错误码与文案可复现、可定位

## 6. 风险与止损机制

### 6.1 主要风险

- 重构跨度大导致双轨长期并存
- IR 设计过度导致进度失控
- 语义层与链接层改动节奏不同步

### 6.2 止损策略

- 严格阶段化推进，未达验收不进入下一阶段
- 每日保持“可构建、可回退”
- 第 2 周末前必须开始清理 legacy 路径，避免长期双轨

## 7. 建议提交流水（示例）

1. `feat(ir): add module ir skeleton and verifier`
2. `refactor(compiler): split buildIR and lowerIR pipeline`
3. `feat(ir): implement minimal ast to ir builder`
4. `feat(ir): implement lowering to vm chunk`
5. `refactor(driver): switch compile pipeline to ir-first`
6. `refactor(linker): resolve symbols from explicit module metadata`
7. `feat(semantic): finalize module visibility context`
8. `chore: remove legacy codegen and update docs`

---

若后续需要补“字段级接口模板”，建议先在 `module_ir.hpp` 明确以下最小稳定契约：

- 指令枚举与操作数布局
- 函数/块/值 ID 生命周期
- 导入导出符号表示
- 与 `linker::CompileModule` 的映射规则

