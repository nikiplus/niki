# 多文件联编测试集

本目录用于验证 `driver -> linker -> launcher -> vm` 的多文件联编路径。

约束说明（当前阶段）：

- 不支持顶层 `var`
- 顶层仅允许声明（不允许顶层表达式语句）
- 跨文件符号调用当前按失败预期处理（记录已知限制）

## 运行方式

在仓库根目录执行：

```powershell
.\build\NIKI.exe scripts/cases/<group>/<case>
```

其中：

- `<group>` 为 `success` 或 `fail`
- `<case>` 为具体用例目录名

## 用例矩阵

| 用例 | 命令 | 预期 |
|---|---|---|
| success/01_multi_file_basic | `.\build\NIKI.exe scripts/cases/success/01_multi_file_basic` | 成功执行，入口 `main` 返回 `42` |
| success/02_init_order | `.\build\NIKI.exe scripts/cases/success/02_init_order` | 成功执行，多个声明文件可共同联编，`main` 返回 `77` |
| success/03_multi_decl_stable | `.\build\NIKI.exe scripts/cases/success/03_multi_decl_stable` | 成功执行，多个声明文件稳定联编，`main` 返回 `100` |
| fail/link_01_no_entry | `.\build\NIKI.exe scripts/cases/fail/link_01_no_entry` | 链接失败，提示未找到入口函数 `main` |
| fail/link_02_multiple_entry | `.\build\NIKI.exe scripts/cases/fail/link_02_multiple_entry` | 链接失败，提示检测到多个入口函数 `main` |
| fail/link_03_id_conflict | `.\build\NIKI.exe scripts/cases/fail/link_03_id_conflict` | 链接失败，提示符号重复定义 |
| fail/semantic_01_cross_file_call | `.\build\NIKI.exe scripts/cases/fail/semantic_01_cross_file_call` | 语义失败，提示 `Undeclared variable` |
| fail/runtime_01_init_error | `.\build\NIKI.exe scripts/cases/fail/runtime_01_init_error` | 入口运行失败（通过跨文件函数触发除零） |
| fail/runtime_02_entry_error | `.\build\NIKI.exe scripts/cases/fail/runtime_02_entry_error` | 入口函数运行失败（`main` 内除零） |

## 结果记录（执行后回填）

- success/01_multi_file_basic: **通过**；输出 `Expr Result: 42`，EXIT=0
- success/02_init_order: **通过**；输出 `Expr Result: 77`，EXIT=0
- success/03_multi_decl_stable: **通过**；输出 `Expr Result: 100`，EXIT=0
- fail/link_01_no_entry: **通过**；命中 `未找到入口函数"main"`，EXIT=65
- fail/link_02_multiple_entry: **通过**；命中 `重复定义符号："main"` 与 `检测到多个入口函数"main"`，EXIT=65
- fail/link_03_id_conflict: **通过**；命中 `重复定义符号："alpha"`，EXIT=65
- fail/semantic_01_cross_file_call: **通过**；命中 `Undeclared variable`，EXIT=65
- fail/runtime_01_init_error: **通过**；命中 `Runtime Error:Division by zero` 与 `入口函数运行失败`，EXIT=65
- fail/runtime_02_entry_error: **通过**；命中 `Runtime Error:Division by zero` 与 `入口函数运行失败`，EXIT=65

## 当前缺陷清单（本轮新增观察）

- 全局共享 `interner` 重构后，`success/*` 不再出现伪 `符号ID冲突`。
- `link_03_id_conflict` 的失败语义已调整为“重复定义符号”，与当前链接策略一致。
