# 构建与测试手册

## 1. 使用 CMake Presets（推荐）

仓库根目录 [CMakePresets.json](../../CMakePresets.json) 定义：

| Preset | 构建类型 | 输出目录 |
|--------|----------|----------|
| `default` | Debug | `build/` |
| `release` | Release | `build-release/` |

```powershell
cmake --preset default
cmake --build --preset default
```

Release：

```powershell
cmake --preset release
cmake --build --preset release
```

## 2. 不使用 Preset（手动）

```powershell
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## 3. vcpkg 依赖

项目使用 manifest 模式 [vcpkg.json](../../vcpkg.json)。首次配置时 vcpkg 会自动安装：

- fmt
- spdlog
- entt
- gtest

## 4. 产物

| 目标 | 路径（Debug） |
|------|----------------|
| 编译器 CLI | `build/NIKI.exe` |
| 单元测试 | `build/niki_tests.exe` |

## 5. 运行测试

### 5.1 全量 CTest

```powershell
ctest --test-dir build --output-on-failure
```

包含：

- GTest（`gtest_discover_tests` 发现 `niki_tests`）
- `layer_boundaries`
- `smoke_e2e`
- `cases_*`（12 个核心联编用例）

### 5.2 仅运行部分测试

```powershell
ctest --test-dir build -R TypeChecker
ctest --test-dir build -R cases_
ctest --test-dir build -R smoke_e2e
```

### 5.3 分层边界（自定义目标）

```powershell
cmake --build build --target check_layer_boundaries
```

## 6. 端到端 smoke

```powershell
.\build\NIKI.exe scripts\smoke
echo $LASTEXITCODE   # 期望 0
```

## 7. 手工 cases 矩阵

```powershell
.\build\NIKI.exe scripts\cases\success\01_multi_file_basic
.\build\NIKI.exe scripts\cases\fail\link_01_no_entry
# 期望失败用例退出码 65
```

矩阵与历史结果见 [scripts/cases/README.md](../../scripts/cases/README.md)。

使用脚本驱动（与 CTest 相同逻辑）：

```powershell
python scripts/run_case.py build/NIKI.exe scripts/cases/success/01_multi_file_basic 0
python scripts/run_case.py build/NIKI.exe scripts/cases/fail/link_01_no_entry 65
```

## 8. VS Code / clangd

- `compile_commands.json` 在 `build/` 下生成（Ninja + CMake）
- [.vscode/settings.json](../../.vscode/settings.json) 指向 `build` 作为 compile commands 目录

## 9. 诊断输出格式

```powershell
.\build\NIKI.exe scripts\smoke --diagnostic-format=json
```

失败时退出码 `65`；用法错误 `64`。
