# M0 工程基线 Playbook

本文档是 **M0（阻断修复与可构建）** 的权威验收与操作说明。完成 M0 后，任何功能开发都应在该绿基线上进行。

## 验收清单

| 项 | 命令 / 条件 | 状态 |
|----|-------------|------|
| Preset 配置 | `cmake --preset default` 成功 | 必选 |
| 构建 | `cmake --build --preset default` 成功 | 必选 |
| 单元与集成测试 | `ctest --test-dir build --output-on-failure` 全绿（204 项，含 GTest + cases + smoke） | 必选 |
| 端到端 smoke | `.\build\NIKI.exe scripts\smoke` 退出码 `0` | 必选 |
| 分层边界 | CTest `layer_boundaries` 通过 | 必选 |
| CI | `.github/workflows/ci.yml` 在 `windows-latest` 绿 | 必选 |

## 环境变量

| 变量 | 说明 |
|------|------|
| `VCPKG_ROOT` | vcpkg 根目录；[CMakePresets.json](../../CMakePresets.json) 的 `vcpkg-base` 预设通过 `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` 引用 |

也可在配置时覆盖：

```powershell
cmake --preset default -D CMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

## 目录约定

| 路径 | 用途 |
|------|------|
| `build/` | Debug 构建输出（Preset `default`） |
| `build-release/` | Release 构建输出（Preset `release`） |
| `scripts/smoke/` | M0 端到端 smoke（仅含最小 `.nk`，避免与 `scripts/main.nk` 等多入口冲突） |
| `scripts/cases/` | 多文件联编手工/自动用例矩阵 |
| `scripts/run_case.py` | CTest 用例驱动：断言进程退出码 |

> **说明**：原 `scripts/test.nk` 为全特性演示脚本（位运算、字符串拼接、结构体等），部分特性尚未在 IR 层完整实现，**不作为 M0 smoke**。M0 smoke 使用 `scripts/smoke/test.nk`。

## CTest 注册的端到端用例（M0 核心 12 + smoke + boundaries）

**成功（退出码 0）**

- `cases_success_01_multi_file_basic`
- `cases_success_02_init_order`
- `cases_success_03_multi_decl_stable`
- `cases_success_04_dice_basic`
- `cases_success_06_comment_tokens`

**失败（退出码 65，与 `main.cpp` 一致）**

- `cases_fail_link_01_no_entry`
- `cases_fail_link_02_multiple_entry`
- `cases_fail_link_03_id_conflict`
- `cases_fail_semantic_kits_duplicate_alias`
- `cases_fail_semantic_01_cross_file_call`
- `cases_fail_runtime_01_init_error`
- `cases_fail_runtime_02_entry_error`

**其他**

- `smoke_e2e` — `scripts/smoke`
- `layer_boundaries` — `scripts/check_layer_boundaries.py`

其余 `scripts/cases` 目录可手工执行，见 [scripts/cases/README.md](../../scripts/cases/README.md)。

## CI

推送/PR 至 `main` 或 `master` 时，GitHub Actions 执行：

1. 克隆 vcpkg 至 `C:\vcpkg`
2. `cmake --preset default` → `cmake --build --preset default` → `ctest --test-dir build`

本地复现 CI 行为：在干净环境中设置 `VCPKG_ROOT` 后执行上述三条命令。

## 主编排入口（当前实现）

```
CompilerOrchestrator::runProject
  → parse / predeclare / module context / typecheck
  → compileParsedBackend (IR → Verify → LowerToChunk)
  → Linker::link
  → Launcher::launchProgram → VM
```

代码锚点：[src/meta/orchestrator/compiler_orchestrator.cpp](../../src/meta/orchestrator/compiler_orchestrator.cpp)。

## 常见问题

**Q: `cmake --preset default` 报找不到 toolchain**  
A: 确认 `VCPKG_ROOT` 已设置且含 `scripts/buildsystems/vcpkg.cmake`。

**Q: ctest 中 `smoke_e2e` 失败**  
A: 确认已构建 `NIKI` 目标；检查 `scripts/smoke/test.nk` 是否仅使用 M0 子集语法。

**Q: 需要 Release 构建**  
A: `cmake --preset release` 与 `cmake --build --preset release`，输出在 `build-release/`。

## 下一步（M1）

M0 完成后进入 [MILESTONES.md](../../MILESTONES.md) 中的 M1：收紧 `Unknown` 恢复、未实现 AST 明确拒绝、opcode 闸门审计等。规划文档见 `docs/planning/`，不作为 M0 阻塞项。
