# NIKI 所有权与借用系统规划

> 本文档定义 NIKI 语言 VM 运行时堆对象（String/Array/Map/Instance）的**所有权（Ownership）与借用（Borrowing）** 内存管理方案。参考 Rust 的核心语义，但不追求 1:1 复刻——仅针对 NIKI 实际需要（跑团数值模拟、组件式游戏逻辑）做适配裁剪。
>
> 关联文档：
> - `docs/planning/dod_aggressive_refactor_plan.md`（DoD 重构路线）
> - `docs/architecture/layering_ir_contract.md`（L0-L5 分层约束）

---

## 0. 为什么需要所有权系统？

### 0.1 当前的内存管理问题

当前 `object.hpp` 采用裸 `malloc`/`free`/`realloc` 手管对象，存在三个风险：

| 问题 | 表现 | 严重程度 |
|------|------|----------|
| **内存泄漏** | `allocateString`/`allocateArray` 分配后从未配对释放 | MVP 可接受，但长期不可持续 |
| **悬空指针** | `OP_FREE` 释放 Instance 但其他寄存器可能仍持有引用 | 运行时偶发 UB |
| **双重释放** | 同对象被多次 `OP_FREE` 或在释放后被再次使用 | 崩溃级 |

VM 层面的 `OP_FREE` 已经存在，但它是一种"无约束的暴力释放"——它只负责释放指针指向的内存，不关心是否还有其他地方在引用同一块内存。

### 0.2 为什么要用所有权系统而不是 GC？

- **游戏/模拟器场景**：GC 的暂停时间和不可预测性对实时交互不友好
- **确定性销毁**：结构体实例和数组在离开作用域后应立即释放，懒惰的 GC 会导致物理内存被无用对象长期占用
- **与 NIKI 的设计气质匹配**：NIKI 语言本身追求控制力，所有权的"显式转移 + 编译期检查"比运行时 GC 更符合语言哲学
- **已存在的骨架**：`BorrowExpr`、`is_owned`、`is_moved`、`OP_FREE` 已在代码中存在，说明所有权设计从一开始就是架构意图的一部分

---

## 1. 设计目标

### 1.1 核心语义

```
# 所有权规则（仿 Rust 核心子集）
1. 每个堆对象有且只有一个所有者（owner）。
2. 当所有者离开作用域，对象被自动释放（drop / OP_FREE）。
3. 所有权可以转移（move），转移后原所有者不可再使用。
4. 可以通过借用（borrow）获得对象的 &ref 或 &mut ref，借用期内所有者不可做 move 或 &mut 借用。
```

### 1.2 适用范围（第一阶段）

| 对象类型 | 纳入所有权 | 说明 |
|---------|-----------|------|
| `ObjString` | 是 | 字符串拼接和函数返回值常产生临时字符串 |
| `ObjArray` | 是 | 动态数组（`push`/扩容）需要确定性生命周期 |
| `ObjMap` | 是 | Map 同理 |
| `ObjInstance` | 是 | 结构体实例是主要的数据载体 |
| `ObjFunction` | 否 | 函数对象由全局表管理，采用独立生命周期 |
| `ObjStructDef` | 否 | 蓝图存活在 VM 的 `global_objects` 表中 |

### 1.3 不做什么（边界声明）

- **不做完整 GC**：第一阶段没有标记-清扫或引用计数。所有权系统是唯一的回收手段。
- **不做生命周期标注**：NIKI 目前不需要 Rust 的 `'a` 生命周期泛型，借用检查通过**单函数内的局部借用分析**完成。
- **不做 `Box<T>` / `Rc<T>` / `RefCell<T>` 智能指针**：第一阶段只处理最简单的情况——值语义 + 所有权转移。

---

## 2. 当前实现状态快照

### 2.1 已完成的骨架

| 组件 | 文件 | 状态 |
|------|------|------|
| `BorrowExpr` AST 节点 | `ast_payloads.hpp` | 已定义，含 `is_mut` 和 `operand` 字段 |
| `is_owned` / `is_moved` 符号标记 | `type_checker.hpp:Symbol` | 已定义，但 `declareSymbol` 默认 `is_owned=false` |
| `checkBorrowExpr()` 方法签名 | `type_checker.hpp` | 已声明，实现为空 stub |
| `endScope()` 作用域退出 | `type_checker.cpp` | 实现存在，但只弹符号栈，不发射 OP_FREE |
| `OP_FREE` opcode | `opcode.hpp` | 已定义 |
| `OP_FREE` VM 实现 | `vm.cpp` | 已实现（裸 free，无借用检查，有潜在 UB） |
| `InstKind::Free` 或等效 IR 指令 | `builder.hpp` / `builder.cpp` | 不存在 —— IR 层面从未发射 free |

### 2.2 链路缺口（需要补齐的部分）

```
Parser                           ✅ BorrowExpr 可被解析
    ↓
TypeChecker.checkBorrowExpr()   ❌ stub（未做借用检查）
    ↓
TypeChecker.endScope()          ❌ 不发射 free（只弹符号）
    ↓
IRBuilder                        ❌ 没有 emitFree / 没有处理所有权转移
    ↓
IRVerify                         ❌ 没有 free 合法性校验规则
    ↓
LowerToChunk                     ❌ 没有 Free 指令到 OP_FREE 的映射
    ↓
VM.OP_FREE                       ✅ 已实现但不安全（裸 free）
```

---

## 3. 分阶段路线图

### 阶段 A：TypeChecker 所有权追踪（优先级最高）

**目标**：让 TypeChecker 能正确标记变量的所有权状态，并在作用域结束时产生 free 标记（不直接发 IR，而是在 AST node_types 或 payload 上做标注）。

**具体任务**：

1. **完善 `declareSymbol` 逻辑**——当变量声明类型为需要所有权的堆对象时，自动标记 `is_owned = true`。
   - 判定条件：局部变量（非全局、非参数）且类型为 Array/Map/Instance/String。
   - 需要 `TypeResolver` 能判断一个 `NKType` 是否是"堆类型"。

2. **跟踪所有权转移**——在 `checkAssignmentStmt` 中：
   - 当 `a = b` 且 `b` 的类型是堆对象时，将 `b.is_moved = true`。
   - 在 `resolveSymbol` 中：如果查到 `is_moved == true` 的符号，报错"use of moved value"。

3. **实现 `checkBorrowExpr`**——在 `type_checker_expr.cpp`：
   - 从 `BorrowExprPayload` 中读取 `is_mut` 和 `operand`。
   - 在校验阶段：如果已存在该符号的 `&mut` 借用或所有权已被转移，禁止再次借用。
   - 第一版只做简单的"借用计数"：符号上记录当前活跃借用数 + 是否有 `&mut` 借出。

4. **`endScope` 发射 free 指示**——当 `endScope` 扫描到 `is_owned && !is_moved` 的局部变量时，将它的 name_id / vreg 写入一个"需要在此作用域退出点 free"的列表。
   - 存储方式：在 `TypeChecker` 上维护 `std::vector<uint32_t> pending_free_in_current_scope;`

**验收标准**：
- [ ] `var a = [1, 2, 3];` → `a` 被标记为 `is_owned=true`
- [ ] `var a = [1, 2, 3]; var b = a; a;` → 编译错误"use of moved value"
- [ ] `var a = [1, 2, 3]; { var b = a; } a;` → 编译错误（所有权已移入内层作用域被释放）
- [ ] `var a = [1, 2, 3]; var b = &a;` → 通过
- [ ] `var a = [1, 2, 3]; var b = &mut a; a;` → 编译错误（&mut 借用期内不能使用所有者）

---

### 阶段 B：IR Builder 发射 Free 指令

**目标**：将 TypeChecker 标注的 free 点转化为 IR 层面的指令。

**具体任务**：

1. 在 `InstKind` 枚举（`module_ir.hpp`）中新增 `InstKind::Free` 指令类型：
   - 操作数：`dst` = 要释放的寄存器号（vreg）。
   - 语义：运行时将该寄存器指向的对象释放，并将寄存器置为 Nil。

2. 在 `IRBuilder` 中新增 `emitFree(bc, fc, vreg)` 方法：
   - 写入 `InstKind::Free` 指令和对应的 vreg 操作数。
   - 在 `buildStmt` / `buildRoot` 中对**每个函数体和每个 BlockStmt 的退出点**，读取 TypeChecker 留下的 free 列表并发射 Free 指令。

3. 修改 `buildStmt` 中的 BlockStmt 处理分支：
   - 在每个代码块的末尾（块内所有语句处理完毕后），发射当前作用域需要 free 的指令序列。

**验收标准**：
- [ ] `func test() { var a = [1,2,3]; }` 的 IR 中包含 `Free a` 指令
- [ ] `func test() { var a = [1,2,3]; var b = a; }` IR 中只有 `Free b`（所有权的转移）
- [ ] nested blocks：内外层作用域分别正确发射 Free

---

### 阶段 C：IR Verify 补充 Free 校验规则

**目标**：增加对 Free 指令合法性的 verify 检查。

**具体任务**：

1. 在 `verifyModuleIRFlat` 中增加规则：
   - Free 的操作数必须是已分配的 vreg（不能是未初始化的寄存器）。
   - Free 不得出现在块中间（只能作为块的终结指令组的一部分，确保释放后该寄存器不会被再次访问）。
   - 同一个 vreg 不得被 Free 两次。

**验收标准**：
- [ ] 双重 Free 的 IR 能被 verify 检出并报错
- [ ] Free 未初始化寄存器能被 verify 检出

---

### 阶段 D：LowerToChunk 映射 Free → OP_FREE

**目标**：将 IR 的 `InstKind::Free` 映射为 VM 的 `OP_FREE` 字节码。

**具体任务**：

1. 在 `lower_to_chunk.cpp` 中增加 `InstKind::Free` 的 case 分支：
   - 读取 vreg 操作数。
   - 发射 `OP_FREE` + vreg 字节码（VM 的 OP_FREE 已支持寄存器参数）。

**验收标准**：
- [ ] IR 的 Free 指令正确生成 `OP_FREE` 字节码
- [ ] VM 执行后对象确实被释放，寄存器被置 Nil

---

### 阶段 E：VM OP_FREE 安全加固

**目标**：让 VM 层面的 `OP_FREE` 更加安全，消除 UB。

**具体任务**：

1. **移除裸 `std::free`**——至少将 `object.hpp` 中的分配器独立为 `allocator.hpp`，所有分配/释放走统一接口。
2. **给 `OP_FREE` 增加预检**——释放前检查寄存器中的 Value：
   - 如果已经是 Nil，直接跳过（允许重复 free 安全地空操作）。
   - 如果 type 不是 Object，报 RUNTIME_ERROR。
3. **引入分配追踪**——在 `ObjFunction` 或 VM 上维护一个 `std::unordered_set<void*>` 分配表：
   - 所有 `allocate*` 调用都在表中登记。
   - `OP_FREE` 从表中移除指针，释放内存。
   - VM 销毁时，表中残留的就是泄漏对象，可做统计/告警。
   - 这是 MVP 轻量方案，后续可替换为真正的 GC。

**验收标准**：
- [ ] 两次 free 同一个对象不会崩溃（第二次是空操作）
- [ ] free Nil 或非对象 Value 会报运行时错误
- [ ] VM 析构时可报告泄漏总量

---

### 阶段 F：BorrowExpr 的完整实现（语言层特性）

**目标**：让 `&x` 和 `&mut x` 成为真正可用的语言特性，而不仅仅是语义检查。

**具体任务**：

1. 完善 `checkBorrowExpr` 的类型推导——返回类型应当是"借用类型"（NKType 附加借用标记），让后续表达式知道这是一个引用而非所有者。
2. IR Builder 中——借用在 IR 层面不需要产生新对象，它只是记录"这个寄存器当前被借用了"，不需要为它分配新堆内存。实际上借用的作用是完全在编译期（类型检查阶段）的。
3. 确定 `&mut x` 的运行时含义——对当前 MVP 来说，`&mut` 也只是编译期约束，不产生运行时开销。后续如果支持写引用（类似 `&mut x` 修改内存），才需要在 IR 层面增加 Borrow/Unborrow 指令。

**验收标准**：
- [ ] `&x` 在类型层面被检查，不产生运行时开销
- [ ] `&x` 与 `x.move_to_other_func()` 在编译期冲突

---

## 4. 与 DoD 重构的关系

所有权系统的各路工作与 `docs/planning/dod_aggressive_refactor_plan.md` 的对应关系：

| DoD 阶段 | 所有权相关工作 |
|---------|---------------|
| 阶段 0：冻结 | 冻结当前 `object.hpp` 的分配/释放接口，不改动 |
| 阶段 3：builder 管线化 | 在 Pass C（语句降级）中集成 Free 发射 |
| 阶段 4：lowering 重构 | 在 LowerToChunk 中增加 Free → OP_FREE 映射 |
| 阶段 5：VM 执行核重排 | 同时加固 OP_FREE 实现 + 引入分配追踪 |

所有权系统**不依赖于 DoD 重构**——它可以作为独立工作流提前推进，只要：
- TypeChecker 阶段（阶段 A）不依赖 IR 数据结构变化
- IR Builder（阶段 B）和 LowerToChunk（阶段 D）的修改与 DoD IR 表化工作存在接口协商

---

## 5. 实现优先级与建议顺序

```
紧急度: 高                     紧急度: 低
  │                              │
  ▼                              ▼
阶段A → 阶段B → 阶段D → 阶段E → 阶段C → 阶段F
(TypeChk) (IR)   (Lower)  (VM)  (Verify) (Borrow)
```

**为什么 A 优先于 E**：TypeChecker 的 `endScope` 标记是"源头控制"，没有它 IR Builder 没有输入。VM 层面的安全加固是锦上添花，不能代替编译期拦截。

**建议的首批执行条目**：

1. 确定 NKType 中"堆类型"的判定方法（`isHeapType()`）
2. 修改 `declareSymbol`，对堆类型自动设置 `is_owned=true`
3. 在 `checkAssignmentStmt` 中加入 `is_moved` 追踪
4. 修改 `resolveSymbol`，遇到 `is_moved` 时报错
5. 在 `endScope` 中收集待 free 符号列表
6. 在 `InstKind` 中新增 `Free`，在 IRBuilder 中发射
7. LowerToChunk 映射
8. VM 安全加固

---

## 6. 边界情况与设计决策记录

### 6.1 函数参数的所有权

```
func consume(arr: Array<int>) { ... }
```

参数 `arr` 的所有权属于调用方还是被调方？

**决策**：NIKI 的参数默认是**借用（& 语义）**，不是 move。调用方仍保有所有权。

若想要 move 语义，用户需要显式写法（语法待定，可能是 `func consume(move arr: Array<int>)`）。第一阶段所有参数都不转移所有权。

### 6.2 返回值的所有权

```
func makeArray() -> Array<int> { return [1, 2, 3]; }
```

**决策**：返回值的所有权转移给调用方。调用方接收值的变量被标记为 `is_owned=true`。

### 6.3 字面量的所有权

```
var a = [1, 2, 3];   // a 是所有者
const b = [1, 2, 3]; // b 的所有权如何处理？
```

**决策**：`const` 声明的堆对象变量**仍拥有所有权**，但它禁止被 `&mut` 借用和禁止 move。`const` 语义是"值不可变"而非"不拥有"。

### 6.4 结构体字段的所有权

```
struct Player { inventory: Array<Item> }

var p = Player([item1, item2]);
var items = p.inventory;  // move 还是 copy？
```

**决策**：字段访问 `p.inventory` 使字段值的所有权移出结构体。读取字段后，结构体的该字段变为不可用。后续若需保留，需设计 `p.inventory.clone()` 之类的显式拷贝方法（第一阶段暂不支持）。

### 6.5 借用周期的约束

**决策**：借用只在**最内层表达式求值期间**有效。一旦表达式求值完成，借用自动结束。不需要 Rust 的生命周期标注。

```niki
var a = [1, 2, 3];
print(len(&a)); // &a 只在 len(&a) 求值期间活跃
a.push(4);      // 通过，因为 &a 已失效
```

### 6.6 循环内的借用

```niki
loop {
    var a = [1, 2, 3];
    var b = &a;
    // b 在每次循环迭代结束后自动失效
    // a 在本轮迭代结束时释放
}
```

**决策**：循环每轮迭代独立管理作用域，借用不跨迭代。

---

## 7. 长期展望（M4+）

- 引入类似 Rust `Drop` trait 的自定义析构语义（在结构体实例释放时自动调用某个方法）
- 支持 `clone()` 方法的显式拷贝
- 跨函数调用的借用分析（目前不做，因为需要函数签名中的生命周期信息）
- 分配器追踪替换为真正的不分代标记-清扫 GC（作为可选降级路径，支持所有权系统的"逃生舱"）

---

## 附录 A：NIKI 语言语法特性全表（基于现有代码实现）

> 此附录基于 2026-04 代码库中 **实际已实现** 的 Scanner/Parser/AST/Semantic/IR 代码，按 Pipeline 链路完整性分类。
>
> 评级标准：
> - **✅ 完整**：Scanner + Parser + Semantic + IR + Lower + VM 全链路闭合
> - **⚠️ 过半**：可解析 + 语义检查但不生成 IR 或 VM 未实现
> - **❌ 部分**：仅 Syntax 层存在，语义或下游缺失
> - **🚫 未实现**：代码中存在声明但实际为 stub

### A.1 顶层声明

| 特性 | 语法形式 | 链路状态 | 代码文件/位置 |
|------|---------|---------|-------------|
| `func` | `func name(params) -> Type { body }` | ✅ 完整 | `parser_declaration.cpp:103` |
| `struct` | `struct Name { field: Type, }` | ✅ 完整 | `parser_declaration.cpp:153` |
| `enum` | `enum Name { Variant1, Variant2 }` | ❌ stub | `parser_declaration.cpp:199` (返回空节点) |
| `type` | `type Alias = Type;` | ✅ 完整 | `parser_declaration.cpp:204` |
| `interface` | `interface Name { methods... }` | ❌ 部分 | `parser_declaration.cpp:220`, semantic stub |
| `impl` | `impl Type [for Trait] { funcs }` | ❌ 部分 | `parser_declaration.cpp:234`, semantic stub |
| `import` | `import Module;` / `import {sym} from Module;` | ✅ 完整 | `parser_declaration.cpp:264` |
| `export` | `export { sym };` / `export func/struct/...` | ✅ 完整 | `parser_declaration.cpp:314` |

### A.2 NIKI 领域特定声明（L1 domain）

| 特性 | 语法形式 | 链路状态 | 代码位置 | 测试用例 |
|------|---------|---------|---------|---------|
| `module` | `module Name { decls... }` | ✅ 完整 | `parser_declaration.cpp:389` | 多文件用例 |
| `component` | `component Name { body }` | ⚠️ 过半 | `parser_declaration.cpp:446`, `analyzer.cpp:19`, `ir.cpp:74` | ✅ 有 |
| | 或 `component StructName as CompName;` (struct 提升) | | | |
| `kits` | `kits Name { [&]Comp as alias; ... }` | ⚠️ 过半 | `parser_declaration.cpp:499`, `analyzer.cpp:57`, `ir.cpp:9` | ✅ 有 |
| `system` | `system Name (deps) { body }` | ❌ 部分 | `parser_declaration.cpp:425`, `analyzer.cpp:9`, **IR 层不识别** | ❌ 无 |
| `flow` | `flow Name { body }` | ❌ 部分 | `parser_declaration.cpp:480`, semantic/IR stub | ❌ 无 |
| `tag` | `tag Name;` | ❌ 部分 | `parser_declaration.cpp:548`, semantic stub | ❌ 无 |
| `taggroup` | `taggroup Name { tags }` | 🚫 未实现 | `parser_declaration.cpp:557` (返回空节点) | ❌ 无 |

### A.3 语句

| 特性 | 语法形式 | 链路状态 | 代码位置 |
|------|---------|---------|---------|
| `nock` | `nock;` / `nock expr;` | ❌ 部分 | `parser_statement.cpp:301`, semantic stub |
| `attach` | `attach component target;` (语义待定) | 🚫 未实现 | `parser_statement.cpp:318` **死代码** |
| `detach` | `detach component target;` (语义待定) | 🚫 未实现 | `parser_statement.cpp:320` **死代码** |
| `await` | `await expr;` (语义待定) | ❌ 部分 | AST 节点 `AwaitExpr` 存在 |
| `& borrow` | `&x` / `&mut x` | ❌ 部分 | AST 节点 `BorrowExpr` 存在 |

### A.4 NIKI 关键字（Scanner 已识别并映射的完整列表）

```
as, any, await, async, bool, break, case, component, const, continue,
else, enum, exclusive, export, false, float, flow, for, from, func,
if, impl, import, int, interface, kits, loop, match, module, nock,
nil, read, return, set, string, struct, system, tag, taggroup, true,
type, unset, var, void, with, write
```

> 注意：`attach`/`detach` **不在关键字列表**中——尚未在 Scanner 中注册。这意味着 `parseAttachStmt`/`parseDetachStmt` 在 `parseStatement` 中没有被调用，是死代码。

---

## 附录 B：关键 Bug 清单（2026-04 代码审查发现）

### B.1 `parseSystemDecl` 写入错误的 Union 成员

**位置**：`src/l0_core/syntax/parser_declaration.cpp:430`

```cpp
ASTNodeIndex Parser::parseSystemDecl() {
    ...
    // ❌ BUG: 应使用 payload.system_decl.name_id，这里写的是 component_decl 成员
    payload.component_decl.name_id = astPool.internString(...);

    // ✅ 下面两行写入 system_decl
    payload.system_decl.system_data = parseExpression(Precedence::None);
    payload.system_decl.body = parseBlockStmt();
    ...
}
```

**原因**：`SystemDeclPayload` 和 `ComponentDeclPayload` 都在 `ASTNodePayload` union 中，前 4 字节都是 `name_id`（`uint32_t`），所以**碰巧值是对的**。但通过非活跃 union 成员写入是未定义行为（UB），编译器开启优化后可能出现诡异行为。

**修复方案**：将 `payload.component_decl.name_id` 改为 `payload.system_decl.name_id`。

### B.2 `parseSystemDecl` 注册了错误的模块名

**位置**：`src/l0_core/syntax/parser_declaration.cpp:430`

即使修复了 B.1 的 UB，当前代码仍存在问题——在 system 解析中将 `name_id` 存入了 `component_decl` union 成员的 `name_id` 位置，但 `ComponentDeclPayload` 更靠后的字段（`is_struct_promotion`、`source_struct_name_id`）如果被误读会导致崩溃。

### B.3 `attach`/`detach` 语句不可达

**位置**：`src/l0_core/syntax/parser_statement.cpp:16-37` 的 `parseStatement()`

```cpp
ASTNodeIndex Parser::parseStatement() {
    // ... 匹配 if, loop, match, continue, break, return, nock ...
    // ❌ 没有 match(TokenType::KW_ATTACH) / match(TokenType::KW_DETACH) 分支
    return parseExpressionStmt();
}
```

**根因**：Token 枚举 `token.hpp` 中没有定义 `KW_ATTACH` 和 `KW_DETACH`。
**修复方案**：在 `token.hpp` 中添加 `KW_ATTACH` / `KW_DETACH`，在 `scanner.cpp` 的关键字 Trie 中添加识别，并在 `parseStatement()` 中添加路由分支。

### B.4 `system` / `flow` / `tag` 不被 IR Builder 识别

**位置**：`src/l0_core/ir/builder_declaration.cpp:71-189` 的 `buildTopDecl()`

`buildTopDecl` 只处理了 6 种顶级声明类型：`ModuleDecl`、`ExportDecl`、`FunctionDecl`、`StructDecl`、`ComponentDecl`、`KitsDecl`。
`SystemDecl`、`FlowDecl`、`TagDecl`、`TagGroupDecl` **被静默忽略**（回退到 `return true`）。

这意味着即使语义检查通过，这些声明的 IR 不会生成，lowering 后 VM 看不到它们。

### B.5 `nock` / `attach` / `detach` 的 IR Builder 无对应分支

`buildStmt` 中不存在 `NockStmt`、`AttachStmt`、`DetachStmt` 的 case 分支，这些语句的语义信息在 IR 层面完全丢失。

---

## 附录 C：所有权与 NIKI 领域特性的交互设计

> 本节讨论所有权/借用系统如何与 `component` / `kits` / `system` / `nock` 等 NIKI 领域特性交互。
> 这些设计决策仍然是探索性的，具体实现时可调整。

### C.1 `component` 与所有权

```niki
component Position { x: int, y: int }
component Velocity { val: int }

var p = Position(10, 20);
```

**决策**：`component` 的**实例**（通过 `OP_NEW_INSTANCE` 创建）是堆对象，纳入所有权管理。`component` 声明本身（蓝图）在 `global_objects` 中，不归所有权管。

当一个 component 实例被赋值给另一个变量时：

```niki
var a = Position(1, 2);  // a is_owned=true
var b = a;                // move: a.is_moved=true, b.is_owned=true
```

### C.2 `kits` 窗口与借用

Kits 窗口定义中的 `&Component as alias`（只读窗口）和 `Component as alias`（可写窗口），其原有的 `is_mutable` 语义和所有权借用系统中的 `&` / `&mut` 语义有天然映射关系：

```niki
kits MyWindow {
    &Position as pos;   // 只读窗口 → 语义等价于 &Position 借用
    Velocity as vel;    // 可写窗口 → 语义等价于 &mut Velocity 借用
}
```

**当前实现**（`analyzer.cpp:74`）：kits 窗口条目的 `is_mutable` 通过 `VarDeclStmt`（可变） vs `ConstDeclStmt`（不可变）区分。
**未来演进**：可将 kits 窗口实现为对 component 实例的**借用集合**——`system` 主体代码通过 kits 别名访问 component 时，typechecker 确保不会违反借用规则。

### C.3 `system` 与所有权

```niki
system MoveSystem (MyWindow) {
    //  通过 pos 和 vel 访问 component 实例
    pos.x = pos.x + vel.val;
}
```

**决策**：`system` 主体内通过 kits 别名访问的 component 实例，其生命周期由**外层作用域**（调用 system 的上下文）管理，`system` 不拥有这些实例的所有权。这是合法的借用场景。

### C.4 `nock` 与所有权

```niki
nock;      // 让出当前帧
nock 5;    // 让出 5 个时间片
```

**决策**：`nock` 语句不改变所有权状态。当前函数的局部变量在 `nock` 期间保持其所有权。`nock` 返回后，所有变量仍然可用。

### C.5 `attach` / `detach` 与所有权

```niki
attach component target;   // 将 component 挂载到一个实体
detach component target;   // 将 component 从实体卸载
```

**决策**：`attach` 将 component 实例的所有权**转交**给目标实体（目标实体成为新的所有者）。`detach` 将所有权**取回**给当前上下文。这本质上是一种作用域跨越的所有权转移，需要特殊处理——当前单函数作用域的 endScope 逻辑不足以覆盖这种场景。

第一阶段（M1-M2）`attach`/`detach` 不会实现（它们甚至还是死代码），因此这个决策可以稍后处理。

### C.6 `async` / `await` 与所有权

```niki
async func fetchData() -> Data {
    // ...
}

var result = await fetchData();
```

**决策**：`async` 函数返回的 future 在 await 完成前持有返回值的所有权。调用方在 await 之前不拥有该值。这需要在 async 上下文中引入跨帧的所有权传递——复杂程度较高，建议在所有权基础系统稳定后再考虑。
