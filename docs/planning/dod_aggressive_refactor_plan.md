# Niki 项目 DoD 激进重构总方案

## 0. 定位与边界

本方案以“强 Data-Oriented Design（DoD）”为目标，迁移窗口内允许阶段性不可运行，但要求语义目标不丢失、每一步可审阅可继续推进。

## 1. 核心原则

- 数据优先于对象：扁平数据表 + 索引优先。
- 连续内存优先：`std::vector` + `u32` ID 引用优先。
- 管线优先于调用栈：以 Pass 边界组织编译链路。
- 批量验证优先：结构合法性集中在 verify pass。
- 结构正确与语义正确分离：分开执行、分开统计。

## 2. 目标架构

- 前端数据域：Token/AST/SourceMap
- IR 数据域：Module/Func/Block/Inst/Value 表
- 执行数据域：Chunk/常量池/执行帧
- Pass 总线：`PassInput` / `PassOutput` / `PassReport`

## 3. 迁移顺序（建议）

1. `verify`
2. `builder`
3. `lower_to_chunk`
4. `vm`

## 4. 分阶段路线图

### 阶段 0：冻结旧路径
- 标记 legacy 路径
- 建立迁移看板
- 明确重构期行为边界

### 阶段 1：IR 存储重排
- AoS 向准 SoA 迁移
- 扁平指令池 + block range
- func/block 元数据表

### 阶段 2：verify 规则表化
- 引入 `InstructionRuleTable`
- 操作数校验查表执行
- 错误码聚合统计

### 阶段 3：builder 管线化
- 拆分 Pass A/B/C/D
- 缩减上下文对象状态
- AST 多轮扫描可暂时接受

### 阶段 4：lowering 重构
- 纯数据变换
- 集中 patch pass
- 常量池批量去重策略

### 阶段 5：VM 执行核重排
- 热冷路径拆分
- 调用帧压缩
- 支持 legacy/new 双核运行切换

### 阶段 6：删旧收口
- 清理 legacy
- 统一文档与测试口径

## 5. 目录建议

- `include/niki/l0_core/ir2/`
- `src/l0_core/ir2/`
- `test/ir2/`

## 6. 测试策略

- L1 结构测试
- L2 pass 快照测试
- L3 verify 错误码测试
- L4 最小运行回归

## 7. 里程碑（结构完成度）

- M1：IR 表化
- M2：verify 规则表化
- M3：builder 管线化
- M4：lowering + vm 新核接通
- M5：删旧收口

## 8. 风险与应对

- 迁移方向漂移 -> 周度里程碑复盘
- 误报/漏报增长 -> 错误码 diff 对比
- 性能误判 -> M4 后再做性能结论
- 风格回潮 -> DoD 评审清单门禁

## 9. 首批执行任务

1. 建立 `ir2` 骨架
2. 迁移终结符规则到规则表
3. 迁移操作数 kind 校验到规则表
4. 引入 `VerifyIssue` 延迟格式化
5. 新增 `test/ir2/verify_rule_table_test.cpp`
6. 补充字段迁移映射文档

## 10. 备注

该文档为迁移版精简方案，后续若需恢复完整任务分解，可在本路径继续扩展，不再回写 `docs/` 根目录。
