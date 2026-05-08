# Niki 文档索引

本文档作为仓库文档总入口，按“功能”和“所属领域”组织。

## 1. 架构与分层（Architecture）

- `docs/architecture/layering_ir_contract.md`
  - L0-L5 分层约束
  - 插件扩展钩子契约
  - IR 统一收敛与演进规则
- `docs/architecture/module_link_feature_vision.md`
  - 模块语义与可见性能力愿景
  - import/export/module/system/component/flow/kits 实现状态
- `docs/architecture/linker_evolution_plan.md`
  - Linker 现状契约、运行不变量、风险与 L0–L5 演进路线（含落盘中间产物与增量链接）

## 1.1 物理层目录（代码）

- 语言核：`include/niki/l0_core` + `src/l0_core`
- 元编排总目录：`include/niki/meta` + `src/meta`
  - `meta/precompile`（解析/预声明/模块语义上下文）
  - `meta/orchestrator`（编译编排）
- 链接与进程内启动：`niki::linker::Linker` / `niki::runtime::Launcher`（实现位于 `l0_core/linker`、`l0_core/runtime`）
- 元编排命名空间：`niki::meta::precompile` / `niki::meta::orchestrator`
- 旧兼容头入口（`include/niki/orchestrator`、`include/niki/project`、`include/niki/runtime_host`）已移除
- `driver` 物理层已移除，统一由 `meta/orchestrator` 提供编排入口
- 领域扩展：`include/niki/l1_domain` + `src/l1_domain`

## 2. 诊断与错误体系（Diagnostics）

- `docs/diagnostics/core_error_codes.md`
  - 核心层错误码归档总表
  - 物理文件归类
  - 错误码触发位置索引
- `docs/diagnostics/diagnostic_conventions.md`
  - 诊断字段、上报接口、渲染格式规范

## 3. 项目审计与体检（Audits）

- `docs/audits/project_health_p0_audit.md`
  - P0 风险体检
  - 现状快照与优先级建议

## 4. 规划与迁移（Planning）

- `docs/planning/dod_aggressive_refactor_plan.md`
  - DoD 激进重构路线图
  - 分阶段迁移任务与验收建议
- `docs/planning/ownership_borrowing_plan.md`
  - VM 堆对象的所有权与借用系统规划
  - 当前实现状态快照与链路缺口分析
  - 分阶段实现路线图（TypeChecker → IR → Lower → VM）
  - 边界情况与设计决策记录
  - **附录 A：NIKI 语法特性全表**（按 Pipeline 链路完整性分类，含代码位置）
  - **附录 B：代码审查发现的 Bug 清单**（含 parseSystemDecl union 错写、attach/detach 死代码、IR 层忽略 system 声明等）
  - **附录 C：所有权与 NIKI 领域特性（component/kits/system/nock/async）的交互设计**

## 5. 测试基础设施 (Test Infrastructure)

### 5.1 测试夹具

- `test/test_helpers.hpp`
  - `ExprTestFixture` 共享测试夹具，提供表达式链路全阶段测试方法：
    - `wrapAndParse()` — 将裸语句体包裹为完整编译单元并解析
    - `runTypeCheck()` — 执行预声明 + 类型检查
    - `buildIR()` — 执行类型检查 + IR 构建
    - `compileAndRun()` — 完整编译执行并返回 VM 结果
  - 自动处理 `module → func → body` 的语法包裹，让测试可聚焦表达式/语句语义

### 5.2 测试文件清单（按阶段）

| 阶段 | 文件 | 测试数 | 覆盖内容 |
|------|------|--------|----------|
| Scanner | `test/syntax/scanner_test.cpp` | 6 | 词法扫描全覆盖 |
| Parser | `test/syntax/parser_test.cpp` | 21 | 表达式解析（字面量/一元/二元/优先级/数组/索引/调用/成员/错误恢复） |
| TypeChecker | `test/semantic/type_checker_test.cpp` | 22 | 类型推断与校验（字面量/标识符/二元/一元/return/函数调用/作用域/重复声明） |
| IR | `test/ir/module_ir_test.cpp` | 3 | ModuleIR 基础数据结构 |
| IR | `test/ir/builder_test.cpp` | 12 | IR 指令序列（常量/加减乘除/变量引用/一元运算/比较/复杂表达式） |
| IR | `test/ir/verify_test.cpp` | 3 | IR 校验 |
| IR | `test/ir/lower_to_chunk_test.cpp` | 2 | IR 到 Chunk 降级 |
| Linker | `test/linker/linker_test.cpp` | 2 | 重复符号/缺失入口诊断 |
| Launcher | `test/runtime/launcher_test.cpp` | 1 | 入口查找失败诊断 |
| Diagnostic | `test/diagnostic/diagnostic_test.cpp` | 4 | 诊断映射与渲染 |
| Driver | `test/driver/driver_project_test.cpp` | 5 | 多模块集成/完整编译执行 |
| L1 Domain | `test/l1_domain/domain_split_test.cpp` | — | 领域拆分 |

### 5.3 测试辅助

- `test/syntax/ast_printer.hpp`
  - `ASTPrinter` 类：将 AST 树转为可读字符串，用于解析器测试断言

### 5.4 端到端测试用例 (scripts/cases/)

详见 `scripts/cases/README.md`，包含成功/失败两类用例，覆盖多文件构建、import/export、语义错误、链接错误、运行时错误等场景。

### 5.5 测试改造方案

- `docs/planning/test_evolution_plan.md`
  - 测试改造背景与现状快照
  - 核心纪律：禁止**通过修改测试来掩盖编译器缺陷**
  - 分三波推进路径（语句覆盖→声明覆盖+运行时集成→边界/错误传播）
  - 具体文件级重组清单与验收标准

## 6. 维护规则

- 新文档必须归档到对应领域目录，避免继续堆积在 `docs/` 根目录。
- 跨文档引用统一使用新路径。
- 若文档过期，优先“迁移并更新”或“删除并在索引标注归档状态”。
