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

## 1.1 物理层目录（代码）

- 语言核：`include/niki/l0_core` + `src/l0_core`
- 元编排总目录：`include/niki/meta` + `src/meta`
  - `meta/precompile`（解析/预声明/模块语义上下文）
  - `meta/orchestrator`（编译编排）
  - `meta/project`（项目链接策略）
  - `meta/runtime_host`（宿主执行策略）
- 元编排命名空间统一：`niki::meta::precompile` / `niki::meta::orchestrator` / `niki::meta::project` / `niki::meta::runtime_host`
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

## 5. 维护规则

- 新文档必须归档到对应领域目录，避免继续堆积在 `docs/` 根目录。
- 跨文档引用统一使用新路径。
- 若文档过期，优先“迁移并更新”或“删除并在索引标注归档状态”。
