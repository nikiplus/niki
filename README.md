# NIKI

NIKI 是一门面向数据与系统编排的自研语言及编译器原型。当前仓库实现从词法/语法到语义、IR、字节码降级、链接与 VM 执行的完整主干，主编排入口为 `meta::orchestrator::CompilerOrchestrator`。

## 依赖

- CMake 3.20+
- C++23 编译器（MSVC 2022 / Clang）
- [vcpkg](https://github.com/microsoft/vcpkg)（通过 `VCPKG_ROOT` 提供 toolchain）
- Python 3（CTest 端到端用例与分层边界检查）

Manifest 依赖见 [vcpkg.json](vcpkg.json)：`fmt`、`spdlog`、`EnTT`、`gtest`。

## 快速开始

```powershell
# 设置 vcpkg（示例）
$env:VCPKG_ROOT = "E:\path\to\vcpkg"

cmake --preset default
cmake --build --preset default
ctest --test-dir build --output-on-failure
```

运行编译器（M0 smoke）：

```powershell
.\build\NIKI.exe scripts\smoke
```

完整说明见 [docs/engineering/m0_baseline.md](docs/engineering/m0_baseline.md) 与 [docs/engineering/build_and_test.md](docs/engineering/build_and_test.md)。

## 文档索引

- [docs/docs_readme.md](docs/docs_readme.md) — 文档总索引
- [MILESTONES.md](MILESTONES.md) — 里程碑与验收
- [docs/engineering/m0_baseline.md](docs/engineering/m0_baseline.md) — M0 工程基线 playbook
- [docs/audits/project_health_p0_audit.md](docs/audits/project_health_p0_audit.md) — 健康度审计

## 延伸阅读

- [NIKI 虚拟机设计指北.md](NIKI%20虚拟机设计指北.md)
- [一些碎碎念.md](一些碎碎念.md)
