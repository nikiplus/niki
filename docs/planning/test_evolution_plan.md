# NIKI 测试改造执行方案

> 本文档定义测试改造的背景、核心纪律、分波推进路径，以及具体的文件级重组清单和验收标准。
>
> 关联文档：
> - `docs/planning/dod_aggressive_refactor_plan.md`（DoD 重构路线）
> - `docs/diagnostics/core_error_codes.md`（核心层错误码归档总表）
> - `docs/diagnostics/diagnostic_conventions.md`（诊断字段与上报规范）
> - `docs/README.md`（文档总入口，本文档归档于「5. 测试基础设施」）

---

## 0. 背景

当前 NIKI 测试体系存在**覆盖面积严重不均衡**的问题：

- **表达式解析与类型检查**——经过 40+ 组测试覆盖（A/B 系列），约占全部测试用例数的 60%。
- **语句、顶层声明、多模块交互**——零测试覆盖。所有 `if`/`loop`/`match`/`break`/`continue`/`assignment`/`nock` 语句以及 `struct`/`enum`/`interface`/`impl`/`import`/`export` 等顶层声明在测试体系中完全空白。
- **运行时端到端验证**——仅 4 个用例，全部为整数运算。
- **错误码断言粒度太粗**——`SemanticCode` 和 `ParserCode` 各只有 1 个 `GenericError` 枚举值，无法区分具体错误类型。

项目已进入 Linker 和 Runtime Host 建设阶段，测试空白将直接导致：上层的多模块链接、导入导出、运行时宿主等新功能在缺少下层测试保护的情况下风险极大。

因此需要进行系统性测试改造。**改造的核心不是堆数量，而是建立纪律、补齐骨架、确保每层错误可定位。**

### 0.1 NIKI 语法约束与测试拓扑依赖

NIKI 语言的底层结构决定了测试的拓扑顺序：

| 规则 | 影响 |
|------|------|
| **禁止全局变量** | 所有变量只能在函数体内声明，不存在顶层变量测试 |
| **禁止闭包** | 函数不捕获外层作用域变量，函数声明与表达式体可拆分测试 |
| **表达式只能存在于函数体内** | 所有表达式测试必须经由 `module → func → body` 的包裹链路 |
| **函数只能存在于模块内** | module + func 是测试的最小完整包裹结构 |

当前 `wrapAndParse` 生成的包裹结构：
```cpp
"module __t{func __test_main()->" + return_type + "{" + body + "}}"
```

这个包裹同时隐式依赖 `ModuleDecl` 和 `FunctionDecl` 的解析正确。现有 60+ 组测试全部依赖这条链路，但**从未显式断言包裹结构的正确性**（如 `ModuleDecl` 的 `name_id`、`FunctionDecl` 的 `params` 列表）。

**顶层声明按"是否包含表达式体"分为两组**，它们对表达式/语句测试的依赖完全不同：

```
依赖无关层（不含表达式体，测试可独立进行）
 ├── StructDecl       ──→ 只含字段列表，不含表达式
 ├── EnumDecl         ──→ 只含变体列表，不含表达式
 ├── InterfaceDecl    ──→ 只含方法签名，不含表达式体
 ├── ImportDecl       ──→ 只含模块/符号引用，不含表达式
 ├── ExportDecl       ──→ 只含符号映射，不含表达式
 └── TypeAliasDecl    ──→ 只含类型映射，不含表达式

依赖表达式/语句层（含表达式体，须在表达式/语句稳定后再做）
 ├── ImplDecl         ──→ 方法体 = 语句 + 表达式
 ├── ComponentDecl    ──→ body 可包含声明和表达式
 ├── KitsDecl         ──→ body 可包含表达式
 ├── SystemDecl       ──→ body 可包含语句 + 表达式
 └── FlowDecl         ──→ body 可包含语句 + 表达式
```

这意味着**Struct/Enum/Import/Export/TypeAlias/Interface 的测试可以与语句测试并行**，而 **Impl/Component/Kits/System/Flow 的测试须在表达式和语句稳定后再做**。

下文的分波路径已经按此拓扑依赖重新排列。

### 0.2 已发现的具体链路断点（2026-05-05 审查）

经过对测试文件与实际代码实现的逐层对照审查，发现当前测试体系存在**"各层自证清白、无人串联验证"**的系统性缺口。以下为具体发现：

#### 0.2.1 函数调用全链路断裂（代码已写，VM 执行不通）

| 层 | 代码位置 | 状态 |
|----|---------|:----:|
| Parser | `parser_expression.cpp:347` — `CallExpr` 解析 | 有代码，有测试 (A-16) |
| TypeChecker | `type_checker_expr.cpp:329` — `checkCallExpr` | 有代码，有测试 (B-17) |
| IR Builder | `builder_expression.cpp:259-300` — `InstKind::Call` 发射 | 有代码 |
| Lower | `lower_to_chunk.cpp:600-624` — `OP_CALL` 降级 | 有代码 |
| VM | `vm.cpp:1280-1340` — `OP_CALL` 解释器（栈帧推入/参数滑动窗口） | 有代码 |
| **端到端** | `driver_project_test.cpp` D-3/D-4 | **改为硬断言后失败** |

此前 D-3/D-4 使用软断言（"失败也可接受"），`compileParsedUnit` 返回错误时不会触发 `ASSERT`。2026-05-05 将这些测试改为硬断言后，立即暴露了 VM 中 `OP_CALL` 的调用约定 Bug——**代码已写完整，但各层从未被同一组数据串联验证过**。

#### 0.2.2 控制流（if/loop/match）IR 代码已存在但零端到端测试

| 能力 | Parser 测试 | TypeChecker 测试 | IR Builder 代码 | IR Builder 测试 | 端到端测试 |
|------|:----------:|:---------------:|:--------------:|:--------------:|:---------:|
| if/else | S-1, S-2 | T-1（故意放水） | `builder_statement.cpp:152-190`（完整 CFG） | **无** | **无** |
| loop | S-3, S-4 | **无** | `builder_statement.cpp:192-236`（完整 CFG + break 回填） | **无** | **无** |
| break | S-4 | **无** | `builder_statement.cpp:238-248`（占位跳转 + loop_stack） | **无** | **无** |
| continue | S-5 | **无** | `builder_statement.cpp:250-259`（跳转到 loop.cond） | **无** | **无** |
| match | S-6 | **无** | **未实现** | **无** | **无** |
| 赋值 | S-7 | T-2（类型不匹配） | `builder_statement.cpp:119-150`（支持简单+复合赋值） | **无** | **无** |
| nock | S-8 | **无** | **仅占位，不生成指令** | **无** | **无** |

关键发现：`builder_statement.cpp` 中 if/loop/break/continue 的 CFG 骨架代码**已完整实现**——包括基本块管理（`beginBlock`/`switchBlock`）、条件分支（`emitBranchOnReg`）、跳转（`emitJumpToBlock`）、break 占位跳转的栈式回填——但 `test/ir/builder_test.cpp` 只覆盖表达式，**从未验证过任何语句的 IR 生成**。

#### 0.2.3 TypeChecker 语句测试放水

`type_checker_stmt_test.cpp` 的 T-1（IfStatementParses）使用 `SUCCEED()` 无条件通过，不对 `runTypeCheck` 的返回值做任何断言：

```cpp
TEST(TypeCheckerStmtTest, IfStatementParses) {
    ...
    auto result = fixture.runTypeCheck(unit);
    // 当前 checkIfStmt 只检查表达式但不强制 Bool 类型校验
    // 只要不崩溃即可
    SUCCEED();
}
```

这意味着 TypeChecker 对 if 语句的处理**没有任何有效的正确性验证**——它可以返回成功、返回失败、甚至内部错误后静默传播到下层，测试都会绿灯。

#### 0.2.4 覆盖矩阵与真实状态严重偏离

文档 2.1 节覆盖矩阵将 `if/loop/match/assignment/nock` 的 Parser、TypeChecker、IR Builder 全部标为"空白"，但实际：

- **Parser**：`parser_stmt_test.cpp` 已覆盖所有 8 组语句
- **TypeChecker**：`type_checker_stmt_test.cpp` 覆盖了部分语句（但测试放水或被跳过）
- **IR Builder**：if/loop/break/continue/assignment 的代码**全部存在**，match/nock 缺失

矩阵的"空白"标记掩盖了真实问题：**不是代码没写，是没测**。

#### 0.2.5 根因：测试金字塔倒置

```
        单元测试（各层独立）  ████████████████████  过多
        集成测试（跨两层）    ██                    严重不足
        端到端测试（全链路）  ███                   仅表达式通过
                                                    
        compileAndRun 从未被用于 if/loop/match 验证
```

`compileAndRun` 是测试夹具中唯一能从源码直通 VM 返回值的 API，但它的调用仅出现在 `builder_test.cpp`（2 次，表达式）和 `driver_project_test.cpp`（3 次，表达式+变量）。对 if/loop/match/函数调用，**零次**调用。

---

## 1. 核心纪律

### 1.1 不可违背的铁律

> **当实际编译过程的行为与测试预期不符时，排除测试自身写错（如断言逻辑有 Bug、期望值计算有误、使用了错误的错误码枚举）的情况后，禁止通过修改测试以试图通过测试。必须定位并修复编译器/引擎/VM 的缺陷。**

这条纪律的工程含义是：

| 场景 | 正确做法 | 禁止的做法 |
|------|---------|-----------|
| 类型检查应为 `int`，结果为 `unknown` | 修复 TypeChecker 的类型推导逻辑 | 在测试中改断言为 `unknown` |
| `if` 语句不应通过类型检查，却通过了 | 修复 TypeChecker 的条件类型校验 | 删除该测试用例或改断言为 pass |
| `1 + true` 应报错，却通过了无报错 | 修复 TypeChecker 的二元操作数校验 | 删除测试或将其移到"预期通过"组 |
| `return 42` 应返回 `42`，却返回 `0` | 修复 IR Builder / Lower / VM 中的 Bug | 修改测试的期望值为 `0` |

### 1.2 例外条款

以下情况**允许**修改测试：

1. **测试自身上有 Bug** —— 例如断言逻辑写反了（`EXPECT_TRUE` 应为 `EXPECT_FALSE`）、使用了错误的错误码枚举（如用了 `ScannerCode` 而不是 `SemanticCode`）、期望值计算错误（如把 `1+2*3` 算成 `9`）。
2. **语言规范明确变动** —— 经过团队讨论确认的语言语义变更，需同步更新测试和编译器实现。
3. **测试夹具（Fixture）本身有 Bug** —— 例如 `wrapAndParse` 生成的包裹源文本有语法错误。

### 1.3 错误码断言的最低要求

所有新增的"预期失败的测试"必须断言以下三要素：

```cpp
// ✅ 正确做法：断言 stage + code + severity
auto result = fixture.compileAndRun("return 1 + true;");
ASSERT_FALSE(result.has_value());
const auto &diags = result.error().all();
ASSERT_FALSE(diags.empty());
EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
EXPECT_EQ(diags[0].severity, diagnostic::DiagnosticSeverity::Error);

// ❌ 禁止：只检查 has_value() == false，不检查错误码
```

---

## 2. 当前测试现状快照

### 2.1 真实覆盖矩阵（2026-05-05 更新）

> **注意**：下表基于代码实现状态而非测试覆盖状态。`⚠️代码存在/未测` 表示该层代码已实现但在测试体系中未被验证。

| 语法类别 | 解析器 (Parser) | 类型检查 (TypeChecker) | IR 构建 (Builder) | 运行时验证 |
|---------|:-------------:|:--------------------:|:---------------:|:---------:|
| **表达式 (Expr)** | ✅ A-1~A-23 (23 用例) | ✅ B-1~B-20, E-2~E-3 (23 用例) | ✅ C-1~C-12 (12 用例) | ✅ D-1~D-5 (表达式/变量) |
| **变量声明 (var)** | ✅ 间接测试 | ✅ B-4, B-6, B-7, B-19, B-20 | ✅ C-7, C-12 | ✅ D-2 |
| **常量声明 (const)** | ❌ 空白 | ❌ 空白 | ❌ 空白 | ❌ 空白 |
| **赋值语句 (=, +=等)** | ✅ S-7 | ⚠️ T-2 (部分) | ⚠️代码存在/未测 | ❌ 未测 |
| **If-Else** | ✅ S-1, S-2 | ⚠️ T-1 (测试放水) | ⚠️代码存在/未测 | ❌ 未测 |
| **Loop** | ✅ S-3, S-4 | ❌ 未测 | ⚠️代码存在/未测 | ❌ 未测 |
| **Break/Continue** | ✅ S-4, S-5 | ❌ 未测 | ⚠️代码存在/未测 | ❌ 未测 |
| **Match** | ✅ S-6 | ❌ 未测 | ❌ 未实现 | ❌ 未测 |
| **Return** | ✅ 间接测试 | ✅ B-15, B-16, T-5, T-6 | ✅ C-1~C-12 隐含 | ✅ 间接 |
| **Nock** | ✅ S-8 | ❌ 未测 | ❌ 仅占位 | ❌ 未测 |
| **函数调用** | ✅ A-16 | ✅ B-17 | ⚠️代码存在/未测 | ❌ 断 (D-3/D-4) |
| **Struct 声明** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **Enum 声明** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **Interface/Impl** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **Import/Export** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **Type Alias** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **System / Flow / Tag** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |
| **Component / Kits** | ❌ 未测 | ❌ 未测 | ❌ 未测 | N/A |

**图例**：
- ✅ = 代码存在且已通过测试验证
- ⚠️代码存在/未测 = 代码已实现，但测试未覆盖或测试放水
- ❌ 未测 = 代码已实现但未被测试（或未实现）
- ❌ 空白 = 代码与测试均为空白
- ❌ 断 = 全链路已写但端到端执行失败

### 2.2 错误码断言现状

| 阶段 | 枚举值个数 | GenericError 占比 | 测试中对错误码的断言 |
|------|:---------:|:---------------:|:-----------------:|
| Scanner | 1 | 100%（唯一值） | ✅ 正确断言 `ScannerCode::InvalidToken` |
| Parser | 1 | 100%（唯一值） | ❌ 无任何错误码断言 |
| Semantic | 1 | 100%（唯一值） | ❌ 只检查 `has_value()` |
| IR | 4 | 25% | ❌ 无错误码断言 |
| Linker | 3 | 0% | ✅ 正确断言 `DuplicateSymbol` / `EntryNotFound` |
| Launcher | 3 | 0% | ✅ 正确断言 `EntryLookupFailed` |
| Driver | 2 | 0% | ✅ 正确断言 `NoInput` |

### 2.3 现有测试文件清单

| 文件 | 阶段代码 | 用例数 | 当前状态 |
|------|---------|:------:|:--------:|
| `test/syntax/scanner_test.cpp` | Phase 0 | 7 | 充分 |
| `test/syntax/parser_test.cpp` | Phase A | 23 | 表达式+包裹结构 |
| `test/syntax/parser_stmt_test.cpp` | Phase S | 8 | 语句解析，已存在 |
| `test/syntax/ast_printer.hpp` | — | 辅助工具 | 仅为表达式 |
| `test/semantic/type_checker_test.cpp` | Phase B | 23 | 表达式，错误码已细化 |
| `test/semantic/type_checker_stmt_test.cpp` | Phase T | 6 | 语句类型检查，已存在 |
| `test/ir/builder_test.cpp` | Phase C | 12 | 仅为表达式 |
| `test/ir/verify_test.cpp` | — | 3 | 偏弱 |
| `test/ir/module_ir_test.cpp` | — | 3 | 可接受 |
| `test/ir/lower_to_chunk_test.cpp` | — | 2 | 偏弱 |
| `test/linker/linker_test.cpp` | — | 2 | 充分，含错误码 |
| `test/runtime/launcher_test.cpp` | — | 1 | 偏弱 |
| `test/driver/driver_project_test.cpp` | Phase D | 5 | 含函数调用硬断言(2 项失败) |
| `test/diagnostic/diagnostic_test.cpp` | — | 4 | 充分 |
| `test/l1_domain/domain_split_test.cpp` | — | 1 | 偏弱 |
| `test/helpers/test_helpers.hpp` | — | 夹具 | 仅支持表达式 |

---

## 3. 分波推进路径

### 3.1 总体演进目标

```
当前状态                          第一波后                          第二波 2a 后                   第二波 2b 后                    第三波后
┌─────────────┐                 ┌─────────────┐                 ┌─────────────┐                ┌─────────────┐                 ┌─────────────┐
│ 表达式  >60% │   ───────→  │ 语句  ~40%   │   ──────────→  │ 独立声明~60%│   ──────────→  │ 依赖声明~50%│   ──────────→  │ 全分类 >80%  │
│ 语句    0%   │   低成本       │ 包裹结构 显式 │   (与第一波     │ 表达式 不变 │   核心新增 │ 运行时 ~50% │   深度完善      │ 错误码 >90%  │
│ 声明    0%   │   高收益       │ 错误码 ~50%  │   可并行)       │ 错误码 ~70% │            │ 错误码 ~80% │                │ 边界 >80%    │
│ 错误码  稀疏 │                │ 表达式 不变   │                │             │           │              │                 │ 全链路 稳定  │
│ 运行时  稀疏 │                │              │                │             │           │              │                 │              │
└─────────────┘                 └─────────────┘                 └────────────┘            └─────────────┘                 └─────────────┘
```

### 3.2 第一波：语句覆盖 + 包裹结构显式化 + 错误码细化（低风险高收益）

**目标**：
1. 补齐所有语句的解析和基础类型检查测试
2. 将 Module + Function 从隐式依赖变为显式断言（验证包裹结构的正确性）
3. 细化 `SemanticCode` 和 `ParserCode` 枚举

**具体任务**：

#### 3.2.1 Module + Function 包裹结构显式断言

在 `test/syntax/parser_test.cpp` 末尾追加 2 组测试，验证 `wrapAndParse` 产生的包裹结构正确：

```cpp
// A-22: 验证包裹结构：ProgramRoot → ModuleDecl → FunctionDecl
TEST(ParserExprTest, ModuleFunctionWrapperStructure) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse("return 42;");
    ASSERT_TRUE(unit.root.isvalid());

    auto root_node = unit.pool.getNode(unit.root);
    EXPECT_EQ(root_node.type, NodeType::ProgramRoot);

    auto module_nodes = fixture.findNodes(unit.pool, NodeType::ModuleDecl);
    EXPECT_GE(module_nodes.size(), 1u);

    auto func_nodes = fixture.findNodes(unit.pool, NodeType::FunctionDecl);
    EXPECT_GE(func_nodes.size(), 1u);
}

// A-23: 验证多个顶层 FunctionDecl 共存
TEST(ParserExprTest, MultipleFunctionsInModule) {
    ExprTestFixture fixture;
    std::string source =
        "module __t{"
        "func helper(x:int)->int{return x+1;}"
        "func __test_main()->int{return helper(41);}"
        "}";
    CompilationUnit unit(fixture.interner_);
    unit.source = source;
    unit.source_path = "__test__";
    syntax::Scanner scanner(unit.source, unit.source_path);
    while (true) {
        auto token = scanner.scanToken();
        unit.tokens.push_back(token);
        if (token.type == syntax::TokenType::TOKEN_EOF) break;
    }
    static_cast<void>(scanner.takeDiagnostics());
    unit.pool.source_path = unit.source_path;
    syntax::Parser parser(unit.source, unit.tokens, unit.pool, unit.source_path);
    auto parse_result = parser.parse();
    unit.root = parse_result.root;
    ASSERT_TRUE(unit.root.isvalid());
    auto func_nodes = fixture.findNodes(unit.pool, NodeType::FunctionDecl);
    EXPECT_EQ(func_nodes.size(), 2u);
}
```

#### 3.2.2 新增 `test/syntax/parser_stmt_test.cpp`

覆盖 8 组语句解析测试，验证 AST 结构正确性：

```cpp
// S-1: If-Else（无 else 分支）
// S-2: If-Else（有 else 分支）
// S-3: Loop（有条件）
// S-4: Loop（无条件）+ Break
// S-5: Continue
// S-6: Match（基础模式）
// S-7: 赋值语句 = / += / -=
// S-8: Nock
```

每个测试至少验证：
- `unit.root.isvalid()`
- 目标 `NodeType` 的节点数量 >= 1
- ASTPrinter 字符串中包含关键标记

#### 3.2.3 新增 `test/semantic/type_checker_stmt_test.cpp`

覆盖 6 组语句类型检查：

```cpp
// T-1: If 条件为 Bool 通过
// T-2: If 条件为 Int 报错（类型不匹配）
// T-3: Loop 条件为 Bool 通过
// T-4: Loop 条件为非 Bool 报错
// T-5: 赋值类型匹配通过
// T-6: 赋值类型不匹配报错
```

所有报错测试断言 `stage` + `code` + `severity` 三要素。

#### 3.2.4 细化 `ParserCode` 枚举

在 `include/niki/l0_core/diagnostic/diagnostic.hpp` 中扩展：

```cpp
enum class ParserCode : uint8_t {
    GenericError,
    ExpectedExpression,
    ExpectedStatement,
    ExpectedSemicolon,
    ExpectedIdentifier,
    UnexpectedToken,
};
```

同步更新 `src/l0_core/diagnostic/diagnostic.cpp` 中的 `codeOf(...)` 映射。

同步更新 `docs/diagnostics/core_error_codes.md` 归档表。

#### 3.2.5 细化 `SemanticCode` 枚举

在 `include/niki/l0_core/diagnostic/diagnostic.hpp` 中扩展：

```cpp
enum class SemanticCode : uint8_t {
    GenericError,
    TypeMismatch,
    UndeclaredIdentifier,
    DuplicateDeclaration,
    ArgumentCountMismatch,
    ReturnTypeMismatch,
    MissingTypeAnnotation,
    NotABoolContext,
    InvalidUnaryOperand,
    AssignmentToConst,
};
```

同步更新 `src/l0_core/diagnostic/diagnostic.cpp` 中的 `codeOf(...)` 映射。

同步更新 `docs/diagnostics/core_error_codes.md` 归档表。

#### 3.2.6 重构现有类型检查测试的错误码断言

遍历 `test/semantic/type_checker_test.cpp`，将所有只有 `EXPECT_FALSE(result.has_value())` 的测试改为三要素断言。例如：

```cpp
// B-7: 类型标注不匹配
// 旧：只检查 result.has_value() == false
// 新：
ASSERT_FALSE(result.has_value());
const auto &diags = result.error().all();
ASSERT_FALSE(diags.empty());
EXPECT_EQ(diags[0].stage, diagnostic::DiagnosticStage::Semantic);
EXPECT_EQ(diags[0].code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
```

**验收标准**（第一波）：
- [ ] `test/syntax/parser_test.cpp` 新增 >= 2 组 Module/Function 结构显式断言
- [ ] `test/syntax/parser_stmt_test.cpp` 存在，包含 >= 8 组测试
- [ ] `test/semantic/type_checker_stmt_test.cpp` 存在，包含 >= 6 组测试
- [ ] `ParserCode` 枚举值 >= 6 个（含 GenericError）
- [ ] `SemanticCode` 枚举值 >= 10 个（含 GenericError）
- [ ] 所有新增的"预期失败"测试断言 `stage` + `code` + `severity` 三要素
- [ ] 所有已有类型检查测试中，仅判断 `has_value() == false` 的用例已升级为三要素断言
- [ ] 全部 14 个测试在新枚举值映射下编译通过并运行通过

---

### 3.3 第二波 2a：独立顶层声明覆盖（与第一波可并行，不受语句/表达式稳定度影响）

**目标**：补齐不含表达式体的顶层声明的解析和类型检查测试。这些声明与表达式/语句**无依赖**，可在第一波进行的同时独立推进。

**具体任务**：

#### 3.3.1 新增 `test/syntax/parser_decl_independent_test.cpp`

覆盖 6 组不依赖表达式体的声明解析：

```cpp
// PD-1: Struct 声明（基础字段）
// PD-2: Enum 声明（基础变体）
// PD-3: Type Alias 声明
// PD-4: Interface 声明（含方法签名）
// PD-5: Import 声明（导入模块）
// PD-6: Export 声明（导出符号）
```

#### 3.3.2 新增 `test/semantic/type_checker_decl_independent_test.cpp`

覆盖 4 组独立声明的类型检查：

```cpp
// DTI-1: Struct 字段类型解析
// DTI-2: Import 模块可见性解析
// DTI-3: 未导出符号不可见报错
// DTI-4: 重复 Struct 声明报错
```

**验收标准**（第二波 2a）：
- [ ] `test/syntax/parser_decl_independent_test.cpp` 存在，包含 >= 6 组测试
- [ ] `test/semantic/type_checker_decl_independent_test.cpp` 存在，包含 >= 4 组测试

---

### 3.4 第二波 2b：依赖表达式/语句的声明 + 运行时集成（核心新增）

**前提条件**：第一波（语句测试）已完成，表达式和语句解析/类型检查已稳定。

**目标**：补齐含表达式体的声明的测试，大幅扩展运行时集成测试。

**具体任务**：

#### 3.4.1 新增 `test/syntax/parser_decl_dependent_test.cpp`

覆盖 4 组依赖表达式/语句的声明解析：

```cpp
// DD-1: Impl 声明（方法体含语句）
// DD-2: Component 声明（body 含表达式）
// DD-3: Kits 声明（体含表达式）
// DD-4: System 声明（body 含语句）
// DD-5: Flow 声明（body 含语句）
```

#### 3.4.2 新增 `test/semantic/type_checker_decl_dependent_test.cpp`

```cpp
// DTD-1: Impl 方法签名匹配
// DTD-2: Component 字段类型解析
// DTD-3: 重复函数声明报错
```

#### 3.4.3 新增 `test/runtime/endpoint_test.cpp`（核心新增）

填补运行时验证空白，覆盖所有可运行构造：

```cpp
// R-1: 浮点运算: return 3.14 + 2.86; → 验证值 ≈ 6.0
// R-2: 布尔逻辑: return true && false || true; → 验证值
// R-3: If-Else: if (1 > 0) { return 100; } else { return 0; }
// R-4: 嵌套 If: if (x > 0) { if (x > 10) { return 2; } return 1; }
// R-5: 函数调用链: func f(x:int)->int{...} func g(y:int)->int{...} return g(f(10));
// R-6: 简单递归: func fact(n:int)->int{...} return fact(5); → 120
// R-7: 多变量运算: var a=1; var b=2; var c=a+b; return c;
// R-8: 常量声明使用: const x=10; return x+5;
```

#### 3.4.4 新增 `test/linker/multi_module_test.cpp`

```cpp
// L-1: 两模块链接（模块 A 被模块 B 引用）
// L-2: 多模块同名冲突检测（已存在，增强）
// L-3: 跨模块入口函数可用
```

**验收标准**（第二波 2b）：
- [ ] `test/syntax/parser_decl_dependent_test.cpp` 存在，包含 >= 4 组测试
- [ ] `test/semantic/type_checker_decl_dependent_test.cpp` 存在，包含 >= 3 组测试
- [ ] `test/runtime/endpoint_test.cpp` 存在，包含 >= 8 组运行时验证
- [ ] `test/linker/multi_module_test.cpp` 存在，包含 >= 2 组测试
- [ ] 浮点运算运行时结果与期望值匹配（允许 FP 误差）
- [ ] 控制流（if-else）的运行时行为与预期匹配
- [ ] 递归函数运行时行为与预期匹配

---

### 3.5 第三波：深度完善（边界 + 错误传播 + 深度覆盖）

**目标**：补充边界条件测试、诊断错误在整个 Driver 层的传播断言、已有测试文件的深度增强。

**具体任务**：

#### 3.5.1 新增 `test/driver/driver_error_test.cpp`

覆盖 Driver 层对下层错误传播的诊断断言：

```cpp
// DE-1: 扫描错误 → 传播到 Driver
// DE-2: 解析错误 → 传播到 Driver
// DE-3: 语义错误 → 传播到 Driver
// DE-4: 链接错误 → 传播到 Driver
// DE-5: 启动错误 → 传播到 Driver
// DE-6: 多错误累积 → Driver 聚合
```

#### 3.5.2 新增 `test/runtime/runtime_edge_test.cpp`

边界条件测试：

```cpp
// RE-1: 除零
// RE-2: 超大整数运算
// RE-3: 深层嵌套表达式（>20 层）
// RE-4: 空函数体
// RE-5: 仅 return 无表达式的 void 函数（如存在 void 类型）
// RE-6: 超大递归深度导致栈溢出（如有栈保护机制）
// RE-7: 空模块
```

#### 3.5.3 增强已有测试文件

| 文件 | 增强内容 | 新增用例数 |
|------|---------|:---------:|
| `test/ir/verify_test.cpp` | 空函数体验证、多函数 IR 验证、有缺陷 IR 的验证拒绝 | +4 |
| `test/ir/lower_to_chunk_test.cpp` | 若/else 分支 lowering、循环 lowering、函数调用 lowering | +4 |
| `test/runtime/launcher_test.cpp` | 多入口冲突、入口执行异常 | +2 |
| `test/l1_domain/domain_split_test.cpp` | 更多 domain validator 规则校验 | +3 |
| `test/ir/builder_test.cpp` | 新增语句 IR 构建测试 | +4 |

#### 3.5.4 补充 `ASTPrinter` 覆盖

`test/syntax/ast_printer.hpp` 当前只能打印有限节点类型。需要补充以下节点类型的打印支持：

```
IfStmt, LoopStmt, BreakStmt, ContinueStmt, MatchStmt, AssignmentStmt,
StructDecl, EnumDecl, ImportDecl, ExportDecl, NockStmt
```

**验收标准**（第三波）：
- [ ] `test/driver/driver_error_test.cpp` 存在，包含 >= 6 组测试
- [ ] `test/runtime/runtime_edge_test.cpp` 存在，包含 >= 4 组测试
- [ ] `ASTPrinter` 支持全部已测试的节点类型打印
- [ ] `verify_test.cpp` 新增 >= 4 组测试
- [ ] `lower_to_chunk_test.cpp` 新增 >= 4 组测试

---

## 4. CMakeLists.txt 更新方案

每次新增测试文件后，需要在根 `CMakeLists.txt` 的 `niki_tests` 目标中添加对应的 `.cpp` 文件。

当前 `niki_tests` 的源文件列表在 `CMakeLists.txt:114-127`：

```cmake
add_executable(niki_tests
    test/syntax/scanner_test.cpp
    test/syntax/parser_test.cpp
    test/semantic/type_checker_test.cpp
    test/ir/builder_test.cpp
    test/ir/verify_test.cpp
    test/ir/module_ir_test.cpp
    test/ir/lower_to_chunk_test.cpp
    test/driver/driver_project_test.cpp
    test/linker/linker_test.cpp
    test/runtime/launcher_test.cpp
    test/l1_domain/domain_split_test.cpp
    test/helpers/test_helpers.hpp
)
```

### 第一波后的 CMakeLists.txt 变化

```cmake
# 新增：
#   test/syntax/parser_stmt_test.cpp
#   test/semantic/type_checker_stmt_test.cpp
```

### 第二波 2a 后的 CMakeLists.txt 变化

```cmake
# 新增：
#   test/syntax/parser_decl_independent_test.cpp
#   test/semantic/type_checker_decl_independent_test.cpp
```

### 第二波 2b 后的 CMakeLists.txt 变化

```cmake
# 新增：
#   test/syntax/parser_decl_dependent_test.cpp
#   test/semantic/type_checker_decl_dependent_test.cpp
#   test/runtime/endpoint_test.cpp
#   test/linker/multi_module_test.cpp
```

### 第三波后的 CMakeLists.txt 变化

```cmake
# 新增：
#   test/driver/driver_error_test.cpp
#   test/runtime/runtime_edge_test.cpp
```

---

## 5. 文件重组路线图总结

```
test/
├── helpers/
│   ├── test_helpers.hpp         # ✅ 保留，增强 wrapAndParse 支持多返回类型
│   └── module_builder.hpp       # [第三波] 新增通用模块构造器
│   ├── scanner_test.cpp                # 保留
│   ├── parser_test.cpp                 # 保留，[第一波增强] 新增 Module/Function 显式断言
│   ├── parser_stmt_test.cpp            # [第一波新增] 8 组语句解析
│   ├── parser_decl_independent_test.cpp# [第二波 2a 新增] 6 组独立声明解析（Struct/Enum/TypeAlias/Interface/Import/Export）
│   ├── parser_decl_dependent_test.cpp # [第二波 2b 新增] 4 组依赖声明解析（Impl/Component/Kits/System/Flow）
│   └── ast_printer.hpp                 # [第三波增强] 补充节点打印
│
├── semantic/
│   ├── type_checker_test.cpp                # 保留，[第一波增强] 错误码断言精细化
│   ├── type_checker_stmt_test.cpp           # [第一波新增] 6 组语句类型检查
│   ├── type_checker_decl_independent_test.cpp# [第二波 2a 新增] 4 组独立声明类型检查
│   └── type_checker_decl_dependent_test.cpp # [第二波 2b 新增] 3 组依赖声明类型检查
│
├── ir/
│   ├── builder_test.cpp          # 保留
│   ├── verify_test.cpp           # 保留，[第三波增强]
│   ├── lower_to_chunk_test.cpp   # 保留，[第三波增强]
│   └── module_ir_test.cpp        # 保留
│
├── runtime/
│   ├── launcher_test.cpp         # 保留，[第三波增强]
│   ├── endpoint_test.cpp         # [第二波新增] 8 组运行时验证
│   └── runtime_edge_test.cpp     # [第三波新增] 边界条件
│
├── linker/
│   ├── linker_test.cpp           # 保留
│   └── multi_module_test.cpp     # [第二波 2b 新增] 跨模块链接
│
├── diagnostic/
│   └── diagnostic_test.cpp       # 保留，[第三波增强]
│
├── driver/
│   ├── driver_project_test.cpp   # 保留
│   └── driver_error_test.cpp     # [第三波新增] 错误传播
│
├── l1_domain/
│   └── domain_split_test.cpp     # 保留，[第三波增强]
│
└── test_helpers.hpp              # [已迁移至 helpers/ 目录]
```

---

## 6. 执行检查总表

### 第一波（建议优先级：最高）

| 序号 | 任务 | 文件 | 负责人 | 完成 |
|:----:|------|------|:-----:|:----:|
| 1 | 新增包裹结构显式断言 | `test/syntax/parser_test.cpp` | | [X] |
| 2 | 新增语句解析测试 | `test/syntax/parser_stmt_test.cpp` | | [X] |
| 3 | 新增语句类型检查测试 | `test/semantic/type_checker_stmt_test.cpp` | | [X] |
| 4 | 细化 ParserCode 枚举 | `include/niki/l0_core/diagnostic/diagnostic.hpp` | | [X] |
| 5 | 细化 SemanticCode 枚举 | `include/niki/l0_core/diagnostic/diagnostic.hpp` | | [X] |
| 6 | 更新 codeOf 映射 | `src/l0_core/diagnostic/diagnostic.cpp` | | [X] |
| 7 | 更新错误码归档 | `docs/diagnostics/core_error_codes.md` | | [X] |
| 8 | 重构现有测试的错误码断言 | `test/semantic/type_checker_test.cpp` | | [X] |
| 9 | 更新 CMakeLists.txt | `CMakeLists.txt` | | [X] |
| 10 | 编译确认 + 运行全部测试 | — | | [X] |
| 11 | **补全第二波 (新增)** | — | | 见下方 |
| 11a | 修复 T-1 测试放水（if typecheck 改为硬断言） | `test/semantic/type_checker_stmt_test.cpp` | | [ ] |
| 11b | 修复函数调用 OP_CALL VM Bug | `src/l0_core/vm/vm.cpp` | | [ ] |
| 11c | 新增语句 IR 构建测试（if/loop/break/continue/赋值） | `test/ir/builder_test.cpp` | | [ ] |
| 11d | 新增控制流端到端测试（if/loop/match 通过 compileAndRun） | `test/runtime/endpoint_test.cpp` 或 `test/driver/driver_project_test.cpp` | | [ ] |

### 第二波 2a（建议优先级：高，与第一波可并行推进）

| 序号 | 任务 | 文件 | 负责人 | 完成 |
|:----:|------|------|:-----:|:----:|
| 1 | 新增独立声明解析测试 | `test/syntax/parser_decl_independent_test.cpp` | | [ ] |
| 2 | 新增独立声明类型检查测试 | `test/semantic/type_checker_decl_independent_test.cpp` | | [ ] |
| 3 | 更新 CMakeLists.txt | `CMakeLists.txt` | | [ ] |
| 4 | 编译确认 + 运行全部测试 | — | | [ ] |

### 第二波 2b（建议优先级：中，须在第一波完成后进行）

| 序号 | 任务 | 文件 | 负责人 | 完成 |
|:----:|------|------|:-----:|:----:|
| 1 | 新增依赖声明解析测试 | `test/syntax/parser_decl_dependent_test.cpp` | | [ ] |
| 2 | 新增依赖声明类型检查测试 | `test/semantic/type_checker_decl_dependent_test.cpp` | | [ ] |
| 3 | 新增运行时端点测试 | `test/runtime/endpoint_test.cpp` | | [ ] |
| 4 | 新增跨模块链接测试 | `test/linker/multi_module_test.cpp` | | [ ] |
| 5 | 更新 CMakeLists.txt | `CMakeLists.txt` | | [ ] |
| 6 | 编译确认 + 运行全部测试 | — | | [ ] |

### 第三波（建议优先级：低）

| 序号 | 任务 | 文件 | 负责人 | 完成 |
|:----:|------|------|:-----:|:----:|
| 1 | 新增 Driver 错误传播测试 | `test/driver/driver_error_test.cpp` | | [ ] |
| 2 | 新增运行时边界测试 | `test/runtime/runtime_edge_test.cpp` | | [ ] |
| 3 | 增强已有测试文件 | 多个 | | [ ] |
| 4 | 补充 ASTPrinter 节点覆盖 | `test/syntax/ast_printer.hpp` | | [ ] |
| 5 | 更新 CMakeLists.txt | `CMakeLists.txt` | | [ ] |
| 6 | 编译确认 + 运行全部测试 | — | | [ ] |

---

## 附录 A：新增测试用例模板

### 语句解析测试模板（`parser_stmt_test.cpp`）

```cpp
#include "../helpers/test_helpers.hpp"
#include "ast_printer.hpp"
#include "niki/l0_core/syntax/ast.hpp"
#include <gtest/gtest.h>

using namespace niki::syntax::test;
using namespace niki::syntax;

// S-1: If-Else 解析
TEST(ParserStmtTest, IfElse) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "if (true) { return 1; } else { return 2; }"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto if_nodes = fixture.findNodes(unit.pool, NodeType::IfStmt);
    EXPECT_GE(if_nodes.size(), 1u);
}

// S-2: Loop 解析
TEST(ParserStmtTest, LoopWithBreak) {
    ExprTestFixture fixture;
    auto unit = fixture.wrapAndParse(
        "var x = 0; loop { if (x > 5) { break; } x = x + 1; } return x;"
    );
    ASSERT_TRUE(unit.root.isvalid());
    auto loop_nodes = fixture.findNodes(unit.pool, NodeType::LoopStmt);
    EXPECT_GE(loop_nodes.size(), 1u);
    auto break_nodes = fixture.findNodes(unit.pool, NodeType::BreakStmt);
    EXPECT_GE(break_nodes.size(), 1u);
}
```

### 类型检查错误码断言模板

```cpp
// 预期失败：三要素断言
auto result = fixture.runTypeCheck(unit);
ASSERT_FALSE(result.has_value());
const auto &bag = result.error();
ASSERT_FALSE(bag.empty());
const auto &d = bag.all()[0];
EXPECT_EQ(d.stage, diagnostic::DiagnosticStage::Semantic);
EXPECT_EQ(d.code, diagnostic::codeOf(diagnostic::events::SemanticCode::TypeMismatch));
EXPECT_EQ(d.severity, diagnostic::DiagnosticSeverity::Error);
```

### 运行时验证测试模板（`endpoint_test.cpp`）

```cpp
// 运行时验证：compileAndRun → 断言 VM 返回值
TEST(RuntimeEndpointTest, FloatArithmetic) {
    ExprTestFixture fixture;
    auto result = fixture.compileAndRun("return 3.14 + 2.86;", "float");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().type, vm::ValueType::Float);
    EXPECT_FLOAT_EQ(result.value().as.floating, 6.0f);
}

// 运行时验证：If-Else 控制流
TEST(RuntimeEndpointTest, IfElseControlFlow) {
    ExprTestFixture fixture;
    auto result = fixture.compileAndRun(
        "if (1 > 0) { return 100; } else { return 0; }"
    );
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().as.integer, 100);
}
```
