| 里程碑 | 状态 | 优先级 | 目标日期 | 备注 |
|---|---|---|---|---|
| M0（阻断修复与可构建） | **已完成** | 最高 | — | 见 [docs/engineering/m0_baseline.md](docs/engineering/m0_baseline.md) |
| M1（V0 最小语言闭环） | 进行中 | 高 | +21天 | 语法-语义-编译-VM 主路径闭环 |
| M2（V0.5 工程化与模块闭环） | 进行中 | 高 | +35天 | orchestrator/linker + 多文件项目运行（主干已通，持续加固） |
| M3（V0.9 自举子集） | 未开始 | 高 | +56天 | 用 NIKI 实现“编译器子集”并可跑通 |
| M4（V1 高级能力） | 未开始 | 中 | +84天 | async/borrow/并发冲突等高级语义 |
---
## M0 - 阻断修复与可构建（1周）

**状态：已完成（2026-05-18）**

### 目标

- 建立可复现的构建、测试与端到端 smoke 绿基线。

### 交付物（已完成）

- [x] 提交 [CMakePresets.json](CMakePresets.json)，统一 C++23 + vcpkg toolchain
- [x] 主编排链路可链接运行：`CompilerOrchestrator` → Linker → Launcher → VM
- [x] `ctest` 注册：`diagnostic_test`、12 个核心 `scripts/cases`、`scripts/smoke`、`layer_boundaries`
- [x] GitHub Actions CI（`windows-latest`）
- [x] 工程文档：[README.md](README.md)、[docs/engineering/m0_baseline.md](docs/engineering/m0_baseline.md)、[docs/engineering/build_and_test.md](docs/engineering/build_and_test.md)

### 验收标准

- [x] `cmake --preset default` 成功
- [x] `cmake --build --preset default` 成功
- [x] `ctest --test-dir build` 全绿（204 项）
- [x] `.\build\NIKI.exe scripts\smoke` 退出码 0

> 全特性演示脚本 [scripts/test.nk](scripts/test.nk) 保留作手工探索；M0 smoke 使用 [scripts/smoke/test.nk](scripts/smoke/test.nk)。

### 历史说明（已过时，勿再执行）

- ~~修复 `compiler.cpp`~~：主链路已迁移为 `IRBuilder → Verify → LowerToChunk`
- ~~`driver` 未实现~~：已由 `meta/orchestrator` 替代

---
## M1 - V0 最小语言闭环（2周）
### 目标
- 聚焦核心语言能力，保证“基础语法 + 基础语义 + 基础执行”稳定。
### 交付物
- 表达式：算术/比较/逻辑/位运算（已支持能力补齐边角）。
- 语句：`var/const/if/loop/return/assignment` 完整可用。
- 类型：`int/float/bool/string/void` 一致性校验。
- 编译器与 VM opcode 对齐表（单一真相来源）。
### 验收标准
- [ ] 基础语法可解析并生成 AST
- [ ] TypeChecker 基础规则覆盖率达到可回归水平
- [ ] 基础样例可编译并执行（成功/失败用例都可验证）
- [ ] 未支持特性均有“明确报错”而非静默失败
### 风险与阻塞
- 类型恢复策略过于宽松，可能掩盖真实错误。
### 执行任务拆分
1. TypeChecker 空实现清点与优先级排序（1天）
2. 语句级语义规则补全（2天）
3. 编译器未实现分支收敛（2天）
4. VM 对应 opcode 收口（2天）
5. 回归测试扩展（1天）
---
## M2 - V0.5 工程化与模块闭环（2周）
### 目标
- 从“单文件可跑”升级到“项目级可运行”并加固链接/导入语义。
### 交付物
- `orchestrator`：目录扫描、模块编译调度、错误汇总（`CompilerOrchestrator` 已实现，持续完善）。
- `linker`：字符串池合并、同模块符号冲突、入口决议（MVP 已实现；跨模块冲突策略待明确）。
- 多模块脚本样例（含重复符号、入口缺失等负例）；`scripts/cases` 与 CTest 对齐。
### 验收标准
- [ ] 多 `.nk` 文件项目可稳定运行
- [ ] 符号冲突、入口缺失、多入口能准确报错（含跨模块策略文档化）
- [ ] 链接后字节码在 VM 稳定执行
- [ ] 关键链路具备集成测试（含 `runProject` 级测试）
### 风险与阻塞
- name_id 重映射一旦不一致，会造成隐蔽运行时错误。
### 执行任务拆分
1. orchestrator 错误聚合与导入语义收紧（2天）
2. linker 跨模块冲突策略 + 测试（2天）
3. 异常路径测试扩展（1天）
4. 端到端回归（1天）
---
## M3 - V0.9 自举子集（3周）
### 目标
- 实现“可自举子集”：用 NIKI 写一个精简前端/编译流程并在 NIKI 上运行。
### 交付物
- NIKI 标准库最小集：字符串、数组、map、错误处理。
- “NIKI 实现的编译器子集”：
  - 词法子集
  - Pratt 表达式解析子集
  - 最小 AST 表达
  - 最小字节码发射（或中间表示发射）
- 自举演示脚本与性能基线。
### 验收标准
- [ ] 子集编译器由 NIKI 代码实现并可运行
- [ ] 能编译并执行一个非平凡样例（>200行）
- [ ] 构建流程可重复（文档化）
- [ ] 关键错误有可定位信息
### 风险与阻塞
- 语言特性与标准库能力不足会限制“自举表达能力”。
### 执行任务拆分
1. 定义“自举子集语法白名单”（1天）
2. 标准库最小能力补齐（3天）
3. 子集编译器实现（7天）
4. 自举验证与回归（3天）
---
## M4 - V1 高级能力（4周）
### 目标
- 在自举子集稳定后再引入异步/并发/借用规则。
### 交付物
- `await/nock/async` 执行语义
- 借用跨作用域静态检查
- 并发冲突分析与诊断
### 验收标准
- [ ] 异步语义可运行
- [ ] 借用规则可检查
- [ ] 并发冲突可检测并定位
---
## 自举距离评估（滚动更新）
- 当前估算：**约 35%**（M0 已完成）
- 达到 M1 后：**约 50%**
- 达到 M2 后：**约 65% ~ 70%**
- 达到 M3 后：**约 85% ~ 90%（进入可用自举阶段）**
---
## 变更记录
| 日期 | 变更内容 | 原因 |
|---|---|---|
| 2026-05-20 | M0 cases 扩展：`04_dice_basic`、`link_03_id_conflict` 纳入 CTest；骰子 IR、跨模块链接冲突 | 对齐手工矩阵 |
| 2026-05-18 | M0 完成：Preset、ctest 139 项、smoke、CI、工程文档 | 工程基线收口 |
| 2026-04-17 | 新增 M0~M4 详细执行路线与量化验收标准 | 将“方向性目标”落为“可执行计划”并对齐自举路径 |
