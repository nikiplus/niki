# Module Link 高级特性愿景文档

> 本文档面向“模块与工程语义”的高级特性（`import/export/from/as`、`module/system/component/flow`），用于锚定：
> 1) token -> AST 的落点；
> 2) 当前实现做到哪里；
> 3) 未来要补哪些实现子系统；
> 4) 每条特性接下来逐条讨论时的对齐问题清单。

---

## 1. 总体愿景（Module Link Vision）

在 Niki 里，“模块与工程语义”承担两类核心责任：

- **工程级可见性**：`import/export/from/as` 决定跨文件符号如何进入一个编译单元的可见集合。
- **运行结构载体**：`module/system/component/flow` 让“系统依赖”和“可调度执行脚本”拥有结构化承载，从而把语言层意图映射到编译后端可执行形态。

语言默认语义（统一口径）：

- 默认所有权转移（move by default）。
- 默认私有（private by default）。
- `pub` 表示公开资格；跨 module 可见仍需 `export`。
- `&T` 表示只读借用，`&mut T` 表示可写借用。
- `const` 表示只读绑定语义；窗口默认可写，`&` 前缀表示只读窗口项。

本愿景与当前编译流水线的衔接点应明确写死：

```mermaid
graph LR
  MOD_SCAN[Parse/AST形成] --> MOD_PREDECL[Predeclare: GlobalSymbolTable/GlobalTypeArena]
  MOD_PREDECL --> MOD_VIS[driver.buildModuleSemanticContext]
  MOD_VIS --> MOD_TYPE[TypeChecker::check(visible_symbols)]
  MOD_TYPE --> MOD_IR[IRBuilder consume node_types]
  MOD_IR --> MOD_LINK[Linker resolve exports & entry]
  MOD_LINK --> MOD_RT[Runtime invoke entry]
```

其中：

- `driver.buildModuleSemanticContext` 负责构建每个 unit 的 `UnitVisibleSymbols`（可见符号表）。
- `TypeChecker` 在 `resolveSymbol` 中消费 `visibleSymbols`，完成“编译期可见性一致性校验”。
- `Linker` 只对已编译模块的导出集合进行入口/符号解析（不承担语义类型校验）。

---

## 2. Token -> AST 锚定（Token Contract）

本节规定：对“模块与工程语义”相关 token，编译器必须把它们落到对应的 AST 节点类型（至少在 Parser 阶段成立），否则语义与 IR 无从谈起。

### 2.1 导入/导出：`import/export/from/as`

| TokenType | Parser 语法入口 | 产出的 ASTNodeType（落点） |
|---|---|---|
| `TokenType::KW_IMPORT` | `Parser::parseImportDecl()` | `NodeType::ImportDecl` |
| `TokenType::KW_EXPORT` | `Parser::parseExportDecl()` | `NodeType::ExportDecl` |
| `TokenType::KW_FROM` / `TokenType::KW_AS` | 作为 import/export 声明内部组成 | 属于 import/export 声明 payload 的字段 |

代码依据（Parser）：

- [parser_declaration.cpp](E:/work/appcodes/niki/src/l0_core/syntax/parser_declaration.cpp)：`parseImportDecl / parseExportDecl`

### 2.2 模块/系统/组件/流程：`module/system/component/flow`

| TokenType | Parser 语法入口 | 产出的 ASTNodeType（落点） |
|---|---|---|
| `TokenType::KW_MODULE` | `Parser::parseModuleDecl()` | `NodeType::ModuleDecl` |
| `TokenType::KW_SYSTEM` | `Parser::parseSystemDecl()` | `NodeType::SystemDecl` |
| `TokenType::KW_COMPONENT` | `Parser::parseComponentDecl()` | `NodeType::ComponentDecl` |
| `TokenType::KW_FLOW` | `Parser::parseFlowDecl()` | `NodeType::FlowDecl` |

代码依据（Parser）：

- [parser_declaration.cpp](E:/work/appcodes/niki/src/l0_core/syntax/parser_declaration.cpp)：`parseModuleDecl / parseSystemDecl / parseComponentDecl / parseFlowDecl`

---

## 3. 当前实现范围（Current Status Snapshot）

这里必须把“能 parse + 能参与可见性解析”的最小闭环说清楚：否则后续逐条讨论很容易漂移。

### 3.1 已有的可见性语义闭环（工程语义的最小落点）

- Parser 会把 `ImportDecl/ExportDecl/ModuleDecl` 等节点落成 AST。
- Driver 会在项目级阶段构建 module 语义上下文：
  - `driver.buildModuleSemanticContext`
    - `collectModuleRegistry`
    - `buildModuleExportTable`
    - `resolveVisibleSymbols`
- `resolveVisibleSymbols` 构建每个 unit 的 `UnitVisibleSymbols.tables`（可见符号表）。
- `TypeChecker::check(..., visible_symbols)` 会把 `visibleSymbols` 指针注入当前 checker，并在 `resolveSymbol` 中查找导入/别名后的本地名字。
- `typealias` 类型链路已接通到语义层：
  - Driver 预声明阶段把 `TypeAliasDecl` 作为顶层符号写入 `GlobalSymbolTable`；
  - `TypeChecker::resolveTypeAnnotation` 在类型标注场景支持通过 `globalSymbols/visibleSymbols` 解析 `TypeAlias`。
- `export wall`（导出墙）已落在可见性构建阶段：
  - Driver 的 `buildModuleExportTable` 依据 `ExportDecl`（brace-list 与 wrapped）构建每个 module 的导出集合；
  - `resolveVisibleSymbols` 在导出集合缺失/符号未导出时不再直接报错，而是让 `TypeChecker` 在 `checkImportDecl` 中给出“import 失败”的诊断。

代码依据（Driver + TypeChecker）：

- [driver.cpp](E:/work/appcodes/niki/src/driver/driver.cpp)
  - `buildModuleSemanticContext`
  - `buildModuleExportTable`
  - `resolveVisibleSymbols`
- [type_checker.cpp](E:/work/appcodes/niki/src/l0_core/semantic/type_checker.cpp)
  - `TypeChecker::check(..., visible_symbols)`
  - `TypeChecker::resolveSymbol(...)` 中对 `visibleSymbols->tables` 的查询逻辑

### 3.2 语义/IRBuilder 的缺口（为什么“目前仍是高级特性占位”）

虽然 Parser/Driver 已经能构建可见性上下文，但目前 IRBuilder 的声明降解范围与 TypeChecker 声明检查范围仍不覆盖所有高级声明类型。

已观察到的关键缺口：

- [type_checker_decl.cpp](E:/work/appcodes/niki/src/l0_core/semantic/type_checker_decl.cpp)：多类声明（如 `checkSystemDecl/checkComponentDecl/checkFlowDecl/...`）仍为占位实现。
- [builder_declaration.cpp](E:/work/appcodes/niki/src/l0_core/ir/builder_declaration.cpp)：顶层声明降解目前仅处理函数（`StructDecl` 直接忽略），因此运行期结构（system/component/flow）尚未进入可执行链路。

因此：本愿景文档中的“实现方案”需要从“可见性解析”走向“可执行落地”，而落地至少分三层：

1. **Driver 可见性表正确**（已基本具备框架）
2. **TypeChecker 声明约束完整**（需要补齐占位）
3. **IRBuilder 把声明映射到运行结构**（需要定义 system/component/flow 的 IR 模型）

---

## 4. 特性愿景与实现方案（Per-Feature Cards）

以下每条特性章节都按统一模板组织，便于我们逐条讨论与对齐：

- **愿景**：用户应该如何写；编译器应该保证什么不变量。
- **语义模型**：它属于哪个阶段、对应哪些数据结构。
- **实现步骤**：从现有“可见性解析”到“可执行落地”需要做哪些补丁。
- **使用用例**：最小 `.nk` 片段（可直接用于回归/文档验证）。

### 4.1 `import/export/from/as`

#### 愿景

- `export` 决定模块对外公开哪些顶层符号：`func`、`struct`、`typealias` 等顶层声明都应可被导出（不仅限函数）。
- `import` 决定当前 module 的可见符号来自哪个模块，以及是否允许重命名（`as`）。
- `as` 的别名绑定语义：**仅对“当前 module”生效**，不会影响来源模块。
- 编译器应保证：在 TypeChecker 阶段，任何可见符号的解析结果必须与 Driver/Linker 构建的 `visibleSymbols` 一致。

#### 语义模型

- **Driver 阶段**：
  - `buildModuleExportTable`：构建 `module_id -> export_name -> SymbolRef`
  - `resolveVisibleSymbols`：构建每 unit 的 `UnitVisibleSymbols.tables`
- **TypeChecker 阶段**：
  - `resolveSymbol`：局部符号 -> GlobalSymbolTable -> visibleSymbols（显式导入）

#### 实现步骤（建议讨论顺序）

1. 明确 export 集合规则：`buildModuleExportTable` 需要纳入全部顶层声明集合（至少覆盖 `func/struct/typealias`，后续再扩展到其它顶层声明）。
2. 明确 import item 语义：当前仅支持显式列出导入项；不做星号/默认导出（如将来引入另行定义）。
3. 未导出导入符号的错误阶段与信息：
   - 报错主责：由 `type_checker` 报错（例如“来源模块未导出此名字/本地可见名不可解析”）。
   - 兜底：`linker` 在缺失或可见性构建漏判时给出清晰诊断。
   - 诊断要求：定位到具体的 `import` 条目/别名位置（不让用户猜测到底是哪一个导入名失效）。
4. 模块名不同但导出符号同名的行为：
   - 当用户同时引入多个模块，并且在“当前 module 的可见名空间”里发生冲突（local name 冲突）时，**应显式报错**，提示用户使用 `as` 做显式重命名。
   - 不走“命名空间包裹”隐式方案；而是让用户用 `as` 明确区分。

#### 使用用例

```nk
// mod_a.nk
func inc(x: int) -> int { return x + 1; }
export { inc as inc_a };

// mod_b.nk
import { inc_a as inc } from mod_a;
func main() -> int {
  return inc(41);
}
```

---

### 4.2 `module`

#### 愿景

- `module` 声明作为命名空间容器，承载：
  - `import/export` 声明；
  - module 内部的顶层声明。
- `module` 自带“导出墙（export wall）”：module 之间通过导出集合进行隔离，外部 `import` 只能看到该 module 显式导出的顶层声明。
- `module` 内部本身是一个独立的次生空间：其可见性需求（尤其是 `import`）由 module 自身决定，而不是被外层的可见性默认透传。
- 编译器应保证：
  - module 根节点是语义扫描的边界；
  - module 的 imports 会影响该 module 的可见性表（只对该 module 内部生效）。
  - module 内部默认“同 module 全可见”：module 内部可直接引用 module 内部声明的顶层符号（无需额外 import）。

#### 语义模型

- Parser：`KW_MODULE -> NodeType::ModuleDecl`
- Driver：
  - `collectTopLevelDecls(unit)`：如果外层 ModuleDecl 里恰好包含一个 primary `module <name>`（NodeType::ModuleDecl），则扫描该 module 的 body；否则退回外层 body。
  - module registry 中每个 unit 对应一个 module_id。

#### 实现步骤

1. 明确 module 内顶层声明范围的不变量（例如 import 是否允许出现在非顶层？当前 Parser 约束已拒绝）。
2. 明确 module 的 import/export 归属与作用域边界：
   - `import` 失败必须指向 `import` 语句/条目的源码位置；
   - module 名字冲突必须指向 `module` 声明中的名字标识符位置。
3. 明确 module 内部可见性构建策略：
   - module 内部默认同 module 全可见（module 内声明可直接被解析）；
   - 从其它 module `import` 进入的符号只来自对方导出集合，且遵循 `as` 映射到 module 内 local name。
4. 明确 module-scoped import/export 的语法落位与实现目标：
   - `import/export` 必须允许出现在 `module { ... }` 主体块内（而不是仅限文件最外层/合成根级别）。
   - parser 需要支持在 module 主体块里构建 import/export AST 节点；
   - driver/可见性构建需要以“module 内的 import 列表”为准，而非依赖合成根的顶层 import。

#### 使用用例

```nk
module math {
  func add(a: int, b: int) -> int { return a + b; }
  export { add };
}
```

---

### 4.3 `system`

#### 愿景

- `system` 用于声明系统级逻辑：通常包含依赖表达式（可能决定组件/资源集）以及主体块。
- 设计约束（本轮新增）：`system` 不能直接访问 `component` 数据，必须通过 `kits` 暴露的数据窗口进行读写。
- 编译器应保证：
  - system 主体块中的语句/表达式必须符合 system 的语义约束（例如是否允许某些控制流、借用/并发边界等）。

#### 语义模型（当前已具备的框架点）

- Parser：`KW_SYSTEM -> NodeType::SystemDecl`（主体块是一个 `parseBlockStmt()` 结果）
- Driver/可见性：`system` 节点本身目前仍主要作为“声明节点”被扫描，但是否进入可见性导出/运行结构，还需要我们定义和落地。
- 与 `kits/component` 的边界约束（待落地）：
  - `system` 中若出现对 `component` 字段/实例的直接访问，应由 TypeChecker 报错；
  - `system` 中允许访问的是 `kits` 窗口中声明为可见的数据槽。

#### 实现步骤

1. 在 TypeChecker 声明阶段补齐 `checkSystemDecl`：
   - 明确 system dependencies 表达式的类型规则与可见符号集合。
   - 明确主体块内语句合法性（未来可能与 `nock/await/borrow` 等语义有关）。
2. 在 IRBuilder 中为 system 产出对应的运行结构：
   - 当前 IR builder 顶层只降解函数，需要扩展到 system 的 IR 模型（例如生成可调度条目或入口块集合）。
3. Linker/Runtime 层：决定 system 与 entry 之间的关系：
   - 是否 entry 会触发若干 system？
   - system 是否需要注册到某个调度表？

#### 使用用例

```nk
system movement {
  // 依赖表达式：示意
  with Position, Velocity;
  {
    // 主体块：示意
    return;
  }
}
```

> 注：上述用例仅作为“语法愿景”示意。当前 Parser 已有 `system` 的骨架入口，但具体依赖表达式的语义尚未在 TypeChecker/IRBuilder 完整落地。

---

### 4.4 `component`

#### 愿景

- `component` 声明用于描述组件数据结构或组件运行期容器。
- 设计约束（本轮新增）：`component` 是原始数据承载体，不直接暴露给 `system`；对外可见性由 `kits` 决定。
- 编译器应保证：
  - component 的字段/类型（如果 language 允许在主体块声明）必须可解析；
  - component 相关标识符默认不在 system 主体内直接可见，必须经过 kits 映射后再可见且类型一致。

#### 语义模型（当前现状）

- Parser：`KW_COMPONENT -> NodeType::ComponentDecl`，支持两种声明形态：
  - 直接声明：`component Name { ... }`
  - struct 提升：`component StructName as ComponentName;`
- TypeChecker（已部分落地）：
  - 对 struct 提升形态校验来源 struct 必须可解析；
  - 直接声明必须具备主体块；
  - 提升形态不允许主体块。
- 统一语义（本轮确认）：
  - 无论直接声明还是 struct 提升，最终都归一为 component 身份；
  - `struct` 作为类型定义仍可独立使用，`component` 作为 ECS 存储身份用于 `kits/system` 链路。
  - 允许“一处 struct -> 多 component 身份”（同一 struct 可提升为多个 component 别名）。

#### 实现步骤

1. 定义 component 主体块语法规则（当前 Parser 允许任意 `parseBlockStmt`，但这并不意味着语义有能力处理其中所有节点）。
2. 补齐 TypeChecker：
   - component 内声明的合法性；
   - 类型标注解析；
   - 与 system 的依赖表达式之间的类型一致性。
3. 扩展 IRBuilder：
   - component 运行时布局（是否直接映射到 VM Object/Struct？）

#### 使用用例

```nk
component Position {
  // 语法愿景：主体块承载字段/类型（当前实现未定义字段语法）
  return;
}

struct Vec2 {
  x: int,
  y: int
}
component Vec2 as Position2D;
```

---

### 4.5 `flow`

#### 愿景

- `flow` 是一个可执行流程主体，允许在主体块中出现 `nock` 与 `await`。
- 编译器应保证：
  - `nock`/`await` 只能出现在 flow 的上下文中（或满足特定控制流约束）。
  - flow 主体块能映射为运行期可调度/可暂停结构。

#### 语义模型（当前现状）

- Parser：`KW_FLOW -> NodeType::FlowDecl`（主体块为 `parseBlockStmt()`，且 parser 注释明确“允许 nock 和 await”）
- TypeChecker：仍需要补齐 `checkFlowDecl`，并最终把 `checkNockStmt/checkAwaitExpr` 与 flow 上下文绑定起来。
- IRBuilder：当前顶层降解只落到函数，因此 flow 的“可执行载体”尚未出现。

#### 实现步骤

1. TypeChecker：
   - 在 flow 上下文内开启/标记允许的控制流与语义域；
   - 对 `nock`/`await` 做位置合法性与类型规则校验。
2. IRBuilder：
   - 为 flow 构建一个可被运行时调度的结构（可能与 VM opcode capability 相关）。
3. Runtime：
   - 决定 flow 如何被触发（entry 触发？还是在 system 调度中触发？）

#### 使用用例

```nk
flow tick {
  {
    nock;
    await some_call();
    return;
  }
}
```

---

### 4.6 `kits`

#### 愿景

- `kits` 是一个“可变数据窗口”（mutable data window），用于桥接 `component` 与 `system`。
- 设计动机（Why）：
  - 解耦数据拥有者（component）与数据消费者（system）；
  - 将可见性/可变性/访问边界收敛为可静态检查的语言契约；
  - 避免 system 对 component 的隐式直连与耦合扩散。
- `kits` 决定哪些 component 数据以什么名称/权限暴露给 system。
- 编译器应保证：
  - `system` 仅能通过 kits 访问组件数据；
  - kits 的映射类型必须与 component 字段类型一致；
  - kits 的读写权限（只读/读写）在 TypeChecker 可静态判定。

#### 语义模型（目标）

- Parser：`KW_KITS -> NodeType::KitsDecl`（当前已有骨架入口）。
- 语义定位（What）：
  - kits = **编译期类型视图 + 运行时实例窗口**。
  - 编译期：用于校验字段访问/权限；
  - 运行时：绑定到具体实体组件数据。
- 最小语法（MVP）：
  - kits 定义：`<component> as <alias>;`（默认可写） / `&<component> as <alias>;`（只读）
  - system 绑定：`system Name(KitsA a, KitsB b) { ... }`
  - 首版只做**组件级窗口声明**，字段级窗口留待后续扩展。
- TypeChecker（待落地）：
  - 建立 `component -> kits -> system` 的可见性链路；
  - 拦截 system 对 component 的直连访问；
  - 校验 kits 映射项的类型一致性与可写性。
- IRBuilder/Runtime（待落地）：
  - kits 需要有独立运行期布局（或映射表），供 system 在执行时通过窗口读写。
  - 生命周期：由 system 执行时创建并持有，执行结束销毁（非全局常驻）。
  - 导出墙：kits 遵循 module export 规则，且 kits 自身可被 export/import。

#### 使用用例（语义示意）

```nk
component Position {
  // 假设包含 x/y
}

kits MoveWindow {
  // 语义示意：声明只读窗口项
  &Position as pos;
}

system movement {
  with MoveWindow;
  {
    // 允许：通过 kits 窗口访问
    // pos.x = pos.x + 1;
  }
}
```

> 注：`kits` 当前仍处于语义建模阶段，上述仅用于锚定“system 不能直连 component，必须走 kits”的设计方向。

#### 已决策约束（本轮确认）

- system 直连 component 一律非法，固定诊断子串：
  - `System cannot access component directly; use kits.`
- 同一 system 作用域中 kits alias 冲突一律编译期报错（禁止覆盖）。
- 权限模型首版：默认可写，使用 `&` 前缀声明只读窗口项。
- 不允许 kits 窗口字段地址泄漏到 system 作用域之外。
- `kits` 支持“窗口滑动（window sliding）”语义，但分阶段落地：
  - MVP：仅保留**调度器隐式滑动**（runtime 在每次 system 调度时重绑定窗口光标）；
  - 扩展：按需开放显式滑动原语（建议最小形态 `slide_to(index)`）。

#### 窗口滑动设计（记录）

- 设计必要性判断：
  - 若 system 始终按调度器逐实体执行，隐式滑动已足够；
  - 若需要在单个 system 内主动跳转到不同实体/区间，则需要显式滑动能力。
- 滑动语义定义：
  - 滑动是**窗口句柄重绑定**，不是组件数据搬移；
  - 编译期/IR 层记录符号与索引信息（如 `component_sid`/cursor），运行时再解引用到实际存储槽位。
- 推荐阶段策略：
  - Phase A（当前目标）：仅实现隐式滑动，不新增语言语法；
  - Phase B（后续扩展）：增加 `slide_to(index)`，再评估 `slide_range`/`next` 等批处理语义。
- 安全约束（与借用模型一致）：
  - 同一 `(component, index)` 上可写窗口与其他借用不得并存；
  - 滑动前获得的 kits 引用/字段引用不得跨滑动点继续使用（否则判定为非法逃逸/悬挂引用）。

#### TypeChecker 规则（实现口径）

- 规则名：`KitsReferenceEscapeForbidden`
- 作用域：仅在 `system` 主体内对 kits 暴露别名及其字段访问结果生效。
- 禁止行为（任一命中即报错）：
  - 将 kits 引用（或其字段引用）赋值给 system 外层变量；
  - 将 kits 引用（或其字段引用）作为 `return` 值返回；
  - 将 kits 引用（或其字段引用）写入全局符号/静态存储；
  - 将 kits 引用（或其字段引用）传给可逃逸参数（被调用方可在当前 system 生命周期外持有）。
- 允许行为：
  - 在当前 system 主体内对 kits 字段进行读写（受“默认可写 + `&`只读”约束）；
  - 在不逃逸前提下，将 kits 字段值拷贝到局部临时值并参与计算。
- 统一诊断子串：
  - `Kits reference cannot escape system scope.`
- 与现有规则并行：
  - 若命中“system 直连 component”，优先报：`System cannot access component directly; use kits.`
  - 若通过 kits 访问但发生逃逸，再报 `KitsReferenceEscapeForbidden`。

#### AST 触发点清单（实现分派）

- `AssignmentStmt`
  - 触发：`rhs` 为 kits 引用/字段引用，且 `lhs` 目标不在当前 system 局部生命周期内。
  - 结果：报 `KitsReferenceEscapeForbidden`。
- `ReturnStmt`
  - 触发：`return` 表达式为 kits 引用/字段引用。
  - 结果：报 `KitsReferenceEscapeForbidden`。
- `CallExpr`
  - 触发：实参包含 kits 引用/字段引用，且形参被标记为可逃逸（或无法证明不逃逸）。
  - 结果：报 `KitsReferenceEscapeForbidden`。
- `VarDeclStmt`（补充）
  - 触发：外层/全局声明以 kits 引用初始化。
  - 结果：报 `KitsReferenceEscapeForbidden`。

> 实现建议：在 TypeChecker 中为表达式附加“来源标签（普通值 / kits 引用 / kits 字段引用）”，
> 然后在上述语句节点统一判定是否发生 escape，避免在每个分支重复推导。

#### 最小测试矩阵（MVP）

- 正例 P1：`system` 内通过 kits 读取字段并参与局部计算（应通过）。
- 正例 P2：`system` 内通过 kits 写入 `var` 窗口字段（应通过）。
- 反例 N1：`return kits_alias.field`（应失败，命中 escape）。
- 反例 N2：`global_var = kits_alias.field`（应失败，命中 escape）。
- 反例 N3：`foo(kits_alias.field)` 且 `foo` 形参可逃逸（应失败，命中 escape）。
- 反例 N4：`system` 直接访问 `component`（不经 kits）（应失败，命中直连诊断子串）。
- 反例 N5：同一 `system` 中 kits alias 冲突（应失败，命中 alias 冲突诊断）。

#### 验收口径（首版）

- 诊断子串稳定：
  - 直连 component：`System cannot access component directly; use kits.`
  - kits escape：`Kits reference cannot escape system scope.`
- 语义优先级稳定：先直连规则，后 escape 规则。
- 与 module 导出墙一致：未导出的 kits/component 不可被跨 module system 使用。

#### TypeChecker 落地顺序（执行建议）

- Phase 0（上下文标记）：
  - 在 `checkSystemDecl` 进入/退出时维护 `in_system_context`；
  - 在 `checkKitsDecl` 解析并登记 kits alias 表与权限位（默认可写 / `&`只读）。
- Phase 1（直连拦截）：
  - 在 `checkIdentifierExpr/checkMemberExpr` 中加入“component 直连”判定；
  - 命中即报：`System cannot access component directly; use kits.`
- Phase 2（escape 拦截）：
  - 在 `checkAssignmentStmt/checkReturnStmt/checkCallExpr/checkVarDeclStmt` 增加 escape 检测；
  - 命中即报：`Kits reference cannot escape system scope.`

#### 错误优先级与去重（实现约束）

- 同一表达式同时触发“直连 component”与“kits escape”时，只报直连错误；
- 同一语句多次命中 escape 条件时，按语句节点去重，避免噪声诊断；
- 错误 span 统一落在触发表达式节点（assignment 的 rhs、return expr、call arg）。

#### 回归策略（实现阶段）

- 每实现一个 phase，立即加对应 fail case，避免“全量完工后一起回归”的排障成本；
- 保留固定诊断子串，测试只匹配子串，不绑定完整文案；
- 先补 TypeChecker 规则，再扩 IR/Runtime，确保前端语义先收口。

---

## 5. 讨论清单（逐条讨论用）

> 每章 3~5 个“待我们下一步对齐的问题点”，用于逐条推进实现时保持方向一致。

### 5.1 `import/export/from/as`

1. export 集合：`func/struct/typealias` 等顶层声明都应可导出（不应仅限 `FunctionDecl`）。
2. `as` 的 alias 绑定仅对当前 module 生效，并且不会对来源模块造成任何反向影响（需要在诊断中明确“本地名”语义）。
3. 未导出的导入符号：由 `type_checker` 主责报错，`linker` 作为兜底；并保证诊断信息清晰、定位到 `import` 条目/本地别名。
4. 当多个模块被导入且在当前 module 的 local name 冲突时：
   - 是否由编译器显式报错并要求用户使用 `as` 重命名？

### 5.2 `module`

1. module 自带导出墙后，“import 失败”权威阶段在 TypeChecker：Driver 只负责构建 visibleSymbols 并按 export wall 过滤；当 import 的 local name 无法在 visibleSymbols 中解析时由 `TypeChecker::checkImportDecl` 报错。
2. `import` 失败诊断的 span 规则：必须落在 `import` 语句位置/条目位置（而不是落在 module 根或缺失符号的目标处）。
3. module 名字冲突（例如重复 module name 或与可见名空间冲突）诊断的 span 规则：必须落在 `module` 声明名字标识符的位置。
4. module 内部默认同 module 全可见：这一规则是否在未来扩展更多 NodeType（struct/typealias/...）时保持一致？

### 5.3 `system`

1. system dependencies 表达式的类型模型是什么？其结果如何影响 kits/component/resource 的可见性？
2. system 主体块允许哪些语句/表达式？与 `return/nock/await` 的合法性边界如何划分？
3. system 的运行期载体是“可调度实体”还是“生成多个 entry/函数组合”？
4. system 与 entry 的关系由哪里决定（Driver/Linker/Runtime 哪一层做权威）？
5. （已决策）system 直连 component 的诊断子串固定为 `System cannot access component directly; use kits.`；待定：错误码与 span 精确落点。

### 5.4 `component`

1. component 主体块的具体语法：是否支持声明字段？如果支持，字段如何被表示到 AST/IR？
2. component 的运行期布局映射到 VM：是沿用 `StructDecl` 的 object 布局，还是另有专用布局？
3. 与 system 依赖表达式之间的约束：如何在类型检查阶段保证一致性？
4. （已决策）允许“一处 struct -> 多 component 身份”；同构多身份建议补充命名约定与 lint 提示，避免语义混淆。

### 5.6 `kits`

1. （已决策）MVP 最小语法：`<component> as <alias>` 与 `&<component> as <alias>`，字段级窗口后置。
2. （已决策）kits = 编译期类型视图 + 运行时实例窗口；生命周期由 system 执行期创建/销毁。
3. （已决策）首版权限语义：默认可写，`&` 标记只读。
4. （已决策）kits 遵循 module 导出墙，kits 自身可 export/import。
5. （已决策）不允许窗口字段地址泄漏到 system 作用域之外（与所有权/借用模型一致）。
6. （已决策）窗口滑动分阶段：MVP 仅隐式滑动；显式 `slide_to(index)` 作为后续扩展点。

### 5.5 `flow`

1. flow 上下文如何被 TypeChecker 识别：仅通过 `FlowDecl` 节点作用域，还是还需要更细粒度的语义标记？
2. `nock` 与 `await` 的类型与控制流规则：应由 flow 语义保证，还是由更底层（未来的并发/借用模块）保证？
3. flow 的运行期可暂停结构：VM/IR 中需要哪些 opcode/状态表示来支持它？

---

## 关键代码引用点（文档中的“依据段落”总结）

当我们后续逐条讨论每条高级特性实现时，建议统一从这些入口“回看现状”：

- Parser（token -> AST）：
  - [parser_declaration.cpp](E:/work/appcodes/niki/src/l0_core/syntax/parser_declaration.cpp)
    - `parseImportDecl / parseExportDecl`
    - `parseModuleDecl / parseSystemDecl / parseComponentDecl / parseFlowDecl`
- Driver（模块图与可见性构建）：
  - [driver.cpp](E:/work/appcodes/niki/src/driver/driver.cpp)
    - `buildModuleSemanticContext`
    - `buildModuleExportTable`
    - `resolveVisibleSymbols`
- Semantic（可见性解析/声明检查入口）：
  - [type_checker.cpp](E:/work/appcodes/niki/src/l0_core/semantic/type_checker.cpp)：`resolveSymbol` 对 `visibleSymbols` 的查询
  - [type_checker_decl.cpp](E:/work/appcodes/niki/src/l0_core/semantic/type_checker_decl.cpp)：占位点（未来需要补齐）
- IR（当前降解范围）：
  - [builder_declaration.cpp](E:/work/appcodes/niki/src/l0_core/ir/builder_declaration.cpp)：顶层降解对 system/component/flow 的缺口

