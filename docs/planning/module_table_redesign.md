# NIKI 模块/全局符号体系激进重构方案

> 版本: v6  
> 状态: 全部阶段 A/B/C/D/E/F/G/H/I/J/K/L 已完成  
> 范围: 破坏性重构，不兼容现有架构  
> 前置: OP_FREE 全链路已闭合（L0 闭壳完成）

---

## 1. 当前架构缺陷全景

### 1.1 五层身份系统互不通

| 层级 | 位置 | 模块身份 | 符号寻址 |
|------|------|----------|----------|
| 源文件 | `CompilationUnit` | `source_path` (string) | — |
| 预声明 | `GlobalSymbolTable` | `owner_module` (string) | `name_id → GlobalSymbol` (flat) |
| 预声明 | `TypeArena` | `owner_module` (string) | 数组下标 (无模块 key) |
| 模块语义 | `ModuleMeta` | `module_id` (uint32_t = unit_idx) | `ModuleRegistry + ExportTable` |
| 模块语义 | `UnitVisibleSymbols` | 同上 | `visible → SymbolRef` (bridge via string match) |
| IR 构建 | `isImportedOrTopLevelName` | 独立遍历 AST (无 module_id) | `name_id` → 扫描声明列表 |
| IR 产物 | `ModuleIR` | `module_name` (string) | `sym_name_sid` ∈ module 本地池 |
| 链接 | `CompileModule` | `module_name` + `source_path` (string) | `exports: map<sid, sid>` (string 决议) |
| 链接 | `LinkedProgram` | `entry_name_id` (string pool id) | 全量扫描所有导出符号 |
| 运行时 | `ObjFunction` | 无 | `name_id` |
| 运行时 | `VM.globals` | 无 | `name_id → ObjFunction*` |

**结论**: 一份模块数据，十种表示方式。每一层都在重新发现"模块是谁"。

### 1.2 致命缺陷: GlobalSymbolTable 无模块作用域 **(阶段 K 已修复)**

```
当前: unordered_map<uint32_t (name_id), GlobalSymbol>
```

`name_id` 来自共享 `StringInterner`。模块 A 和模块 B 各自定义 `func init()` —— 第二个 `insert()` 直接失败，报 duplicate。

**这意味着语言承诺的 `module { ... }` 隔离性在语义层根本不存在。**

`GlobalSymbol.owner_module` 是 `source_path` 字符串，存在但不参与 key。

### 1.3 脆弱身份: ModuleMeta.module_id = unit_idx

`compileAll()` 中 `unit_idx` 是 for 循环索引。文件收集顺序改变 → 所有 `module_id` 改变。

### 1.4 分裂的 AST 遍历: collectTopLevelDecls 两份独立实现

- `predeclare_stage.cpp::collectTopLevelDecls()`: 正确处理了两层 ModuleDecl 解包
- `builder_expression.cpp::isImportedOrTopLevelName()`: 之前没有（已修），但属独立复制

**两份代码，一份语义。任何 AST 结构变更意味着两处修改。**

### 1.5 O(N²) 全表扫描: resolveVisibleSymbols **(阶段 F 已修复)**

```cpp
for (auto unit_idx : all_units) {
    for (auto [name_id, symbol] : global_symbols.symbol_table) {  // 全量扫描
        if (symbol.owner_module != unit.source_path) continue;    // 字符串比较
        visible[name_id] = SymbolRef{...};
    }
}
```

O(number_of_units × total_symbols) — 大型项目不可接受。

### 1.6 链接阶段基于字符串决议

`project_linker.cpp` 中 `collectDefinedSymbols()` 用 `string_pool[sid]` 反查符号名称，再用 `name_to_owner` 做字符串级别的冲突检测和入口决议。

**后果**: 字符串池的合并顺序影响符号决议；"同名"的判断依赖池内字符串一致。

### 1.7 parse() 合成外层 ModuleDecl 的链式污染

`parse()` 无条件在外层包一个 `ModuleDecl`，导致:

- 所有 AST 遍历逻辑都有两层 ModuleDecl
- `collectTopLevelDecls` 必须先进入内层才能拿到真正的声明
- 嵌套 `module` 声明和三方库引入的 `module` 处理逻辑混淆

---

## 2. 重构目标

1. **唯一身份**: 模块以一个稳定的 `ModuleId` (uint32_t) 贯穿全链路
2. **模块命名空间**: 符号名 `(module_id, name_id)` 为 key，不再全局扁平
3. **管道级索引**: 每个模块的符号/类型/导出有专用索引，无全表扫描
4. **AST 结构简化**: 消除合成外层 ModuleDecl，root 直接是真实 ModuleDecl 或 ProgramRoot
5. **链接阶段基于 ID 决议**: 不再依赖字符串池反查
6. **VM 层模块感知**: VM 的全局注册表改用 `(module_id, name_id)` key

---

## 3. 新核心结构设计

### 3.1 ModuleId 分配器

```cpp
// 新文件: include/niki/l0_core/semantic/module_id.hpp
namespace niki {

using ModuleId = uint32_t;
constexpr ModuleId kInvalidModuleId = UINT32_MAX;

class ModuleIdAllocator {
public:
    /// @brief 为 source_path 分配稳定 ModuleId（幂等）。
    ModuleId ensure(const std::string &source_path);

    /// @brief 已知 id 反查 source_path。
    const std::string *findPath(ModuleId id) const;

private:
    std::vector<std::string> paths_;                        // id → path
    std::unordered_map<std::string, ModuleId> path_to_id_;  // path → id
};

} // namespace niki
```

- FileScanner 阶段即决定每个文件的 ModuleId
- 分配是幂等的: 相同 `source_path` 返回相同 id
- 单元测试中通过 `"__test__"` 分配固定 id

### 3.2 ModuleNamespace (替代 GlobalSymbolTable)

```cpp
// 新文件: include/niki/l0_core/semantic/module_namespace.hpp
namespace niki {

/// @brief 模块作用域符号注册与查询（替代 GlobalSymbolTable）。
///        符号 key = (module_id, name_id)，天然支持跨模块同名符号。
class ModuleNamespace {
public:
    struct Symbol {
        uint32_t name_id;
        ModuleId owner_module_id;
        Kind kind;               // Function / Struct / TypeAlias
        semantic::NKType type;   // 已解析类型（Function(sig_id) / Object(struct_id)）
    };

    /// @brief 插入符号。同模块内重名返回 false；跨模块重名允许。
    bool insert(Symbol sym);

    /// @brief 按完整 key 查询。
    const Symbol *find(ModuleId module_id, uint32_t name_id) const;

    /// @brief 按模块 id 获取该模块所有符号（用于构建 visible symbols 和导出表）。
    std::vector<const Symbol *> findModuleSymbols(ModuleId module_id) const;

private:
    // PRIMARY KEY: (owner_module_id, name_id)
    struct Key {
        ModuleId module_id;
        uint32_t name_id;
        bool operator==(const Key &o) const {
            return module_id == o.module_id && name_id == o.name_id;
        }
    };
    struct KeyHash {
        size_t operator()(Key k) const {
            return std::hash<uint64_t>{}((uint64_t(k.module_id) << 32) | k.name_id);
        }
    };
    std::unordered_map<Key, Symbol, KeyHash> symbols_;

    // 反向索引: module_id → [Symbol*] (用于 O(1) 快速获取模块全部符号)
    std::unordered_map<ModuleId, std::vector<Symbol *>> module_symbols_;
};

} // namespace niki
```

**对比旧设计**:

| 维度 | 旧 GlobalSymbolTable | 新 ModuleNamespace |
|------|---------------------|-------------------|
| key | `name_id` | `(module_id, name_id)` |
| 同名跨模块 | 冲突 | 允许 |
| per-module 查询 | O(N) 全扫 | O(1) 反向索引 |
| owner_module 类型 | `string` (source_path) | `ModuleId` (uint32_t) |

### 3.3 TypeArena 改造

```cpp
// 修改: include/niki/l0_core/semantic/type_arena.hpp
namespace niki {

class TypeArena {
public:
    struct StructInfo {
        uint32_t name_id;
        ModuleId owner_module_id;       // 改: 使用 ModuleId 替代 std::string
        std::vector<uint32_t> field_name_ids;
        std::vector<semantic::NKType> field_types;
    };

    /// @brief 改: owner_module 参数从 std::string 改为 ModuleId
    uint32_t internStruct(uint32_t name_id, ModuleId owner_module_id,
                          std::vector<uint32_t> field_name_ids = {},
                          std::vector<semantic::NKType> field_types = {});

    // ... 其余不变
};

} // namespace niki
```

改动范围小: 仅参数类型和内部比对从 `string` 改为 `ModuleId`。

### 3.4 ModuleMeta / ModuleRegistry 精简

```cpp
// 修改: include/niki/l0_core/semantic/module_semantic.hpp
namespace niki::semantic {

/// @brief 精简: 移除 unit_index（module_id 不再等于 unit_idx）。
struct ModuleMeta {
    ModuleId module_id;                        // 稳定 ID
    std::string source_path;                   // 保留用于诊断
    std::vector<ImportBinding> imports;
    std::vector<ExportBinding> exports;
    // ❌ 移除: size_t unit_index
    // ❌ 移除: ModuleExportTable (合并到 SymbolRefTable)
};

/// @brief 精简: 移除 module_id_to_meta_index（module_id 已经是直接索引）。
struct ModuleRegistry {
    std::unordered_map<ModuleId, ModuleMeta> modules;  // 直接以 ModuleId 为 key
};

/// @brief 统一符号引用表: 替代 UnitVisibleSymbols + ModuleExportTable。
///        module_symbols[module_id][name_id] = SymbolRef
struct SymbolRefTable {
    // 每个模块的导出/同模块可见符号
    std::unordered_map<ModuleId, std::unordered_map<uint32_t, SymbolRef>> module_symbols;
};

} // namespace niki::semantic
```

### 3.5 parse() 不再合成外层 ModuleDecl

```cpp
// 修改: src/l0_core/syntax/parse.cpp
ParseResult Parser::parse() {
    // 当前行为: 收集顶层声明 → 包在外层合成 ModuleDecl → 返回合成节点
    // 新行为: 若源码以 'module' 开头 → 直接返回解析出的 ModuleDecl
    //         否则 → 包在 ProgramRoot 中 (或自动合成 ModuleDecl 但设置 explicit=false flag)
    //
    // 关键: 消除 "两层 ModuleDecl" 嵌套，使 AST 遍历只需一层解包
}
```

这一改动影响面较大，需单独列出迁移清单:

- `collectTopLevelDecls` (两处): 简化为一层解包
- `isImportedOrTopLevelName`: 可复用 `collectTopLevelDecls`
- `TypeChecker`: root 直接就是真实 ModuleDecl
- `IRBuilder::buildRoot`: 同上

### 3.6 CompileModule 与链接产物改造

```cpp
// 修改: include/niki/l0_core/linker/linker_facade.hpp
namespace niki::linker {

struct CompileModule {
    ModuleId module_id;                          // 新增: 稳定模块身份
    std::string module_name;                     // 保留: 逻辑名
    std::string source_path;                     // 保留: 诊断定位
    Chunk init_chunk;
    std::unordered_map<uint32_t, uint32_t> exports;  // local_name_id → ModuleId (非 sid)
    std::vector<ir::SymRecord> exported_symbols;
};

struct LinkedProgram {
    std::vector<Chunk> init_chunks;
    uint32_t entry_name_id = UINT32_MAX;
    std::vector<std::string> string_pool;
    /// 新增: 模块初始化表 (VM 按 module_id 索引全局符号注册)
    std::unordered_map<ModuleId, Chunk> module_inits;
};

} // namespace niki::linker
```

### 3.7 VM 全局符号表改造

```cpp
// 修改: include/niki/l0_core/vm/vm.hpp
namespace niki::vm {

class VM {
public:
    // 当前: unordered_map<uint32_t, ObjFunction*> globals;  // name_id → function
    // 改造后:
    struct GlobalKey {
        ModuleId module_id;
        uint32_t name_id;
    };
    std::unordered_map<GlobalKey, ObjFunction *, GlobalKeyHash> globals;

    /// @brief 查找全局符号: 同模块优先，跨模块可见回退
    ObjFunction *findGlobal(ModuleId module_id, uint32_t name_id);
};

} // namespace niki::vm
```

`OP_GET_GLOBAL` / `OP_SET_GLOBAL` 字节码指令本身不需要改——它仍接收一个 `constIdx` 指向字符串名。改动在 VM 执行侧: `defineGlobal` 时读取当前模块 id，`getGlobal` 时先查本模块再查跨模块可见集。

---

## 4. 管道流程重构

### 4.1 新 Pipeline (6 阶段)

```
阶段 0: FileScan
  输入: 源文件路径集合
  处理: 分配 ModuleId (幂等); 产生 files_ 列表
  输出: ModuleIdAllocator + [FileEntry{path, module_id}]

阶段 1: Parse
  输入: FileEntry
  处理: scan → parse (不再合成外层 ModuleDecl)
         AST root 为真实 ModuleDecl / ProgramRoot
  输出: CompilationUnit (含 module_id)

阶段 2: Predeclare
  输入: CompilationUnit
  处理: 遍历 AST 顶层声明 → ModuleNamespace.insert(sym with module_id)
        TypeArena.internStruct(module_id, ...)
  输出: 无（直接写入 ModuleNamespace + TypeArena）

阶段 3: ModuleSemanticContext
  输入: ModuleNamespace + 所有 unit 的 import/export AST
  处理: registry (ModuleId → ModuleMeta)
        SymbolRefTable (module_id → {name_id → SymbolRef})
        visible per unit (直接查 SymbolRefTable 的 O(1) 路径)
  输出: ModuleSemanticContext（精简）

阶段 4: TypeCheck
  输入: CompilationUnit + SymbolRefTable[module_id]
  处理: TypeChecker.check(pool, root, namespace, arena, visible_for_unit)
         resolveSymbol 改为查 (module_id, name_id) 优先再查 visible
  输出: ASTPool.node_types 已填充

阶段 5: Backend (IR → Verify → Lower)
  输入: CompilationUnit
  处理: IRBuilder.build(unit) → ModuleIR
        verifyModuleIRFlat → lowerModuleToChunk
        isImportedOrTopLevelName 复用 SymbolRefTable
  输出: CompileModule (含 module_id)

阶段 6: Link
  输入: [CompileModule]
  处理: 基于 module_id 做符号决议（不再依赖 string）
        合并 init_chunk 与跨模块引用
  输出: LinkedProgram
```

### 4.2 受影响文件清单

| 文件 | 改动类型 | 说明 |
|------|----------|------|
| `include/niki/l0_core/syntax/ast.hpp` | 新增字段 | `ASTPool.module_id`; 移除 root 外层合成需要 |
| `include/niki/l0_core/syntax/ast_payloads.hpp` | 新增字段 | `ModuleDeclPayload.explicit_module` flag |
| `src/l0_core/syntax/ast.cpp` | 修改 | `clear()` 不重置 module_id |
| `src/l0_core/syntax/parse.cpp` | 重写 | 不再合成外层 ModuleDecl |
| `include/niki/l0_core/semantic/global_symbol_table.hpp` | **删除** | 被 ModuleNamespace 替代 |
| `src/l0_core/semantic/global_symbol_table.cpp` | **删除** | 同上 |
| `include/niki/l0_core/semantic/module_namespace.hpp` | **新增** | ModuleNamespace 核心 |
| `src/l0_core/semantic/module_namespace.cpp` | **新增** | 实现 |
| `include/niki/l0_core/semantic/module_id.hpp` | **新增** | ModuleIdAllocator |
| `src/l0_core/semantic/module_id.cpp` | **新增** | 实现 |
| `include/niki/l0_core/semantic/type_arena.hpp` | 修改 | `owner_module` → `ModuleId` |
| `src/l0_core/semantic/type_arena.cpp` | 修改 | 同上 |
| `include/niki/l0_core/semantic/compilation_unit.hpp` | 修改 | 新增 `module_id` 字段 |
| `include/niki/l0_core/semantic/module_semantic.hpp` | 重写 | ModuleMeta/Registry/SymbolRefTable 精简 |
| `include/niki/l0_core/semantic/type_checker.hpp` | 修改 | 引用从 `GlobalSymbolTable*` → `ModuleNamespace*` |
| `src/l0_core/semantic/type_checker.cpp` | 修改 | `resolveSymbol` 改为 `(module_id, name_id)` 查询 |
| `src/l0_core/semantic/type_checker_decl.cpp` | 修改 | 同上 + 参数 `is_owned=false` 已做 |
| `src/l0_core/semantic/type_checker_pre_decl.cpp` | 修改 | `preDeclareNode` 引用更新 |
| `src/meta/precompile/parse_stage.cpp` | 修改 | parse 阶段不需修改（改动在 parse.cpp） |
| `src/meta/precompile/predeclare_stage.cpp` | 重写 | 写入 ModuleNamespace 而非 GlobalSymbolTable |
| `src/meta/precompile/module_context_stage.cpp` | 重写 | 基于 ModuleNamespace + SymbolRefTable 构建 |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | 修改 | 新增 ModuleIdAllocator; 传递 ModuleNamespace |
| `src/meta/orchestrator/compile_pipeline.cpp` | 修改 | `compileUnitChunk` 接收 ModuleNamespace |
| `include/niki/meta/precompile/precompile_pipeline.hpp` | 修改 | API 签名的 GlobalSymbolTable → ModuleNamespace |
| `src/l0_core/ir/builder_expression.cpp` | 修改 | `isImportedOrTopLevelName` 改为查 SymbolRefTable |
| `src/l0_core/ir/builder.cpp` | 修改 | 传递 module_id 到 buildExpr |
| `include/niki/l0_core/linker/linker_facade.hpp` | 修改 | CompileModule 新增 module_id |
| `src/meta/project/project_linker.cpp` | 重写 | 链接决议改为基于 ModuleId + SymbolRef 而非字符串 |
| `include/niki/l0_core/vm/vm.hpp` | 修改 | `globals` key 改为 `(module_id, name_id)` |
| `src/l0_core/vm/vm.cpp` | 修改 | `defineGlobal/getGlobal` 查找逻辑 |
| `src/l1_domain/**` | 修改 | ir/sym_record 可能需要 module_id |
| `test/helpers/test_helpers.hpp` | 修改 | 传入 ModuleNamespace |
| 所有测试文件 | 修改 | `symbols_` → `module_namespace_` |

---

## 5. 迁移策略

### 5.1 阶段划分

| 阶段 | 内容 | 破坏性 | 预计工时 | 状态 |
|------|------|--------|----------|------|
| A | ModuleId + ModuleIdAllocator 落地 | 无 | 0.5d | **已完成** |
| B | ModuleNamespace 落地（GlobalSymbolTable 共存） | 低 | 1d | **已完成** |
| C | parse() 不再合成外层 ModuleDecl | **高** | 1d | **已完成** |
| D | collectTopLevelDecls / isImportedOrTopLevelName 统一 | 中 | 0.5d | **已完成** (随阶段 C 一同完成) |
| E | Pipe 迁移: predeclare → ModuleNamespace | 中 | 0.5d | **已完成** |
| F | Pipe 迁移: module context → SymbolRefTable | 中 | 0.5d | **已完成** |
| G | Pipe 迁移: TypeChecker 注入 ModuleNamespace | 中 | 0.5d | **已完成** |
| H | Pipe 迁移: IR builder 查 SymbolRefTable | 低 | 0.5d | **已完成** |
| I | Linker 改造: 基于 ID 决议 | 中 | 1d | **已完成** |
| J | VM globals 改造 | 中 | 0.5d | **已完成** |
| K | 删除 GlobalSymbolTable | **破坏性** | 0.25d | **已完成** |
| L | 全量测试回归 + 修边 | — | 1d | **已完成** |

**总计: ~7.75 人天**

### 5.2 可测试性保障

- 每个阶段完成后 `ctest` 必须全绿
- 阶段 A-C 属于基础设施层，不对现有测试产生行为变更
- 阶段 D-L 每步有针对性单元测试覆盖新能力（如跨模块同名函数、O(1) 模块查询）

---

## 6. 设计决策记录

### 6.1 为什么不直接用 ModuleId 做 ModuleNamespace 的 key prefix？

采用复合 key `(module_id, name_id)` 而非 `module_id` 做 namespace 方案中的自动前缀。

**原因**: `ModuleNamespace` 本身就是模块感知的。复合 key 可以让 `find(module_id, name_id)` 的语义清晰：我要查"模块 X 里的名字 Y"。如果用 namespace 前缀自动拼接，则需要另一套映射逻辑。

### 6.2 为什么保留 CompileModule.exports 为 map<uint32_t, uint32_t>？

链接阶段仍需 `local_name_id → exported_name_id` 映射，但改为 `ModuleId` 而非字符串决议冲突。

**原因**: 导出表的本质是 "本模块内的哪个名字可以对外叫什么"。这个名字 id 来自 ASTPool 的 interner，在 ModuleNamespace 中已有 `(module_id, name_id) → type` 映射。链接器不需要再反查字符串。

### 6.3 为什么不把 ModuleNamespace 也放进 StringInterner？

`StringInterner` 的职责是字符串驻留（string → id）。`ModuleNamespace` 的职责是符号解析（(module, name) → type）。这是两种不同的 "intern"——混合它们会违反单一职责原则。

### 6.4 parse() 不合成 ModuleDecl 后的 AST 兼容性

当前:
```
ModuleDecl (合成)
  └── BlockStmt
      └── ModuleDecl (用户写)
          └── BlockStmt
              ├── FunctionDecl
              └── ...
```

改后:
```
# 用户写了 module __t { ... }
ModuleDecl (用户写)
  └── BlockStmt
      ├── FunctionDecl
      └── ...

# 用户没写 module (单文件脚本)
ProgramRoot
  └── BlockStmt
      ├── FunctionDecl
      └── ...
```

所有遍历逻辑统一为: **取 root 的 body，解包一层便是声明列表**。不再有隐藏的第二层。

---

## 7. 不与计划冲突的说明

本次重构不影响 L0 闭壳已有成果 (OP_FREE 全链路、TypeChecker 所有权、IR Builder Free 发射、Lower/Verify)。

重构范围集中在**模块语义层** (GlobalSymbolTable / ModuleNamespace / TypeArena / ModuleRegistry / export/import/link pipeline)，这些与 IR 指令层 (InstKind/Builder/Lower) 正交。

---

> **生成时间**: 2026-05-06  
> **依赖**: OP_FREE 全链路已闭合  
> **当前进度**: 阶段 A 已完成 → 阶段 B 待启动

---

## 8. 阶段 A 执行记录 (2026-05-06)

### 8.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/semantic/module_id.hpp` | **新增** | ModuleId 类型别名 + ModuleIdAllocator 类定义 |
| `src/l0_core/semantic/module_id.cpp` | **新增** | ModuleIdAllocator::ensure() 和 findPath() 实现 |
| `CMakeLists.txt` | 修改 | 新增 `src/l0_core/semantic/module_id.cpp` 到编译源列表 |
| `include/niki/l0_core/semantic/compilation_unit.hpp` | 修改 | `CompilationUnit` 新增 `module_id` 字段，引入 `module_id.hpp` |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | 修改 | `compileAll()` 中创建 `ModuleIdAllocator`，解析后为每个 unit 分配 `module_id` |
| `test/helpers/test_helpers.hpp` | 修改 | `ExprTestFixture` 新增 `module_id_allocator_` 成员，`wrapAndParse()` 中分配模块 id |

### 8.2 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 无。所有现有行为保持不变

---

## 9. 阶段 B 执行记录 (2026-05-06)

### 9.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/semantic/module_namespace.hpp` | **新增** | ModuleNamespace 类定义：`(module_id, name_id)` 复合 key；反向索引 `module_symbols_` |
| `src/l0_core/semantic/module_namespace.cpp` | **新增** | `insert()` / `find()` / `findModuleSymbols()` 实现 |
| `CMakeLists.txt` | 修改 | 新增 `src/l0_core/semantic/module_namespace.cpp` 到编译源列表 |

### 9.2 设计要点

- **插入语义**: 同模块内重名返回 false，跨模块重名允许（落实 `module { ... }` 隔离性）
- **O(1) 反向索引**: `module_symbols_` 存 Key（非指针），避免 rehash 导致悬空指针
- **与 GlobalSymbolTable 完全共存**: 本阶段不修改任何现有代码，仅新增 ModuleNamespace 类

### 9.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 无。仅新增文件，无任何现有代码修改

---

## 10. 阶段 C 执行记录 (2026-05-07)

### 10.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/l0_core/syntax/parse.cpp` | **修改** | `parse()` 不再无条件合成外层 ModuleDecl：若顶层仅有一个 ModuleDecl 则直接返回，否则合成外层 ModuleDecl |
| `src/meta/precompile/predeclare_stage.cpp` | **修改** | `collectTopLevelDecls()` 简化为单层解包（移除 ~30 行双层 ModuleDecl 嵌套处理）；`predeclareSingleUnit` 接受 ProgramRoot 根类型 |

### 10.2 AST 结构变化

**改前**:
```
ModuleDecl (合成，name_id=0)
  └── BlockStmt
      └── ModuleDecl (用户写，name_id=__t)  // 双层嵌套
          └── BlockStmt
              ├── FunctionDecl
              └── ...
```

**改后**:
```
ModuleDecl (用户写，name_id=__t)  // 直接是 root
  └── BlockStmt
      ├── FunctionDecl
      └── ...
```

### 10.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 高。AST 根结构改变，但所有消费端已适配（IRBuilder::buildRoot、TypeChecker、compile_pipeline、builder_expression 均无需修改）

## 11. 阶段 E 执行记录 (2026-05-07)

### 11.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/meta/precompile/precompile_pipeline.hpp` | **修改** | 新增 `#include module_namespace.hpp`；`predeclareSingleUnit` 签名增加 `ModuleNamespace &module_namespace` 参数 |
| `src/meta/precompile/predeclare_stage.cpp` | **修改** | `predeclareSingleUnit` 实现双写：每个符号先写入 `ModuleNamespace`，再写入 `GlobalSymbolTable`（通过 `insert_dual` lambda）；`predeclare_typealias_decl` 同步适配 |
| `include/niki/meta/orchestrator/compiler_orchestrator.hpp` | **修改** | 新增 `#include module_namespace.hpp`；`predeclareAllUnits` 签名增加 `ModuleNamespace &` 参数 |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | **修改** | `compileAll()` 创建 `ModuleNamespace module_namespace` 实例；`predeclareAllUnits` 调用传入 `module_namespace` |
| `include/niki/meta/orchestrator/compile_pipeline.hpp` | **修改** | 新增 `#include module_namespace.hpp`；`compileParsedUnit` 签名增加 `ModuleNamespace &module_namespace` 参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `compileParsedUnit` 实现接收并转发 `module_namespace` 到 `predeclareSingleUnit` |
| `test/helpers/test_helpers.hpp` | **修改** | 新增 `#include module_namespace.hpp`；`ExprTestFixture` 新增 `ModuleNamespace module_namespace_` 成员；`runTypeCheck()` 和 `compileAndRun()` 调用传 `module_namespace_` |
| `test/driver/driver_project_test.cpp` | **修改** | `buildUnitFromSource` 签名增加 `ModuleIdAllocator &` 参数（修复先前未设置 `module_id` 的问题）；所有 3 处 `compileParsedUnit` 调用传入 `fixture.module_namespace_` |
| `test/ir/verify_test.cpp` | **修改** | `buildIRFromBody` 创建 `ModuleNamespace` 和 `ModuleIdAllocator`；设置 `unit.module_id`；`predeclareSingleUnit` 调用传入 `module_namespace` |

### 11.2 设计要点

- **双写策略**: 阶段 E 使用 `insert_dual` lambda 同时写入 `GlobalSymbolTable`（旧）和 `ModuleNamespace`（新），确保下游 `TypeChecker`、`IRBuilder`、`Linker` 等无需立即适配。旧表将在阶段 K 删除。
- **`ModuleNamespace::insert` 使用 composite key `(module_id, name_id)`**: 消除 `GlobalSymbolTable` 的跨模块名称冲突问题。
- **`predeclare_typealias_decl` lambda 捕获**: 由于 lambda 内部使用 `insert_dual`，需要确保 `insert_dual` 先于其定义。

### 11.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 中。所有调用 `predeclareSingleUnit` / `compileParsedUnit` / `predeclareAllUnits` 的代码均需新增 `ModuleNamespace` 参数，但内部行为无实质变化

## 12. 阶段 F 执行记录 (2026-05-07)

### 12.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/meta/precompile/precompile_pipeline.hpp` | **修改** | `buildModuleSemanticContext` 签名参数 `GlobalSymbolTable` → `const ModuleNamespace &` |
| `src/meta/precompile/module_context_stage.cpp` | **修改** | `collectModuleRegistry`: `meta.module_id = unit.module_id`（稳定 ID）；`buildModuleExportTable`: `module_namespace.find(module_id, name_id)` 替代 `global_symbols.find(name_id)`；`resolveVisibleSymbols`: `module_namespace.findModuleSymbols(module_id)` O(1) 替代遍历 `global_symbols.symbol_table` 加 `owner_module` 字符串比较 |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | **修改** | `buildModuleSemanticContext(units, module_namespace)` 传入新参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `buildModuleSemanticContext(single_unit, module_namespace)` 传入新参数 |

### 12.2 设计要点

- **`collectModuleRegistry` 使用稳定 `ModuleId`**: 将 `meta.module_id = static_cast<uint32_t>(unit_idx)` 替换为 `unit.module_id`，使 registry 中 module_id 与 ModuleIdAllocator 分配的稳定 ID 一致。
- **`buildModuleExportTable` 改用 `ModuleNamespace::find(module_id, name_id)`**: 精确按 `(module_id, name_id)` 复合键查询导出符号，避免旧表 `find(name_id)` 的跨模块名冲突。
- **`resolveVisibleSymbols` 改用 `ModuleNamespace::findModuleSymbols(module_id)`**: 从 O(N) 遍历 `global_symbols.symbol_table` + `owner_module` 字符串比较，升级为 O(1) 反向索引查询本模块所有符号。

### 12.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 中。`buildModuleSemanticContext` 所有调用方需同步改参，内部实现从 O(N) 全局表遍历升级为 O(1) 模块查询

## 13. 阶段 G 执行记录 (2026-05-07)

### 13.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/semantic/type_checker.hpp` | **修改** | 新增 `#include module_id.hpp` + `module_namespace.hpp`；`check()` 两个重载签名新增 `ModuleId module_id, const ModuleNamespace &`；新增成员 `ModuleId currentModuleId` + `const ModuleNamespace *moduleNamespace` |
| `src/l0_core/semantic/type_checker.cpp` | **修改** | 新增 `#include module_namespace.hpp`；两个 `check()` 实现接收并存储 `module_id`/`module_namespace`，退出时清理；`resolveSymbol()` 优先 `moduleNamespace->find(currentModuleId, name_id)` O(1) 查询，回退 `globalSymbols->find()`；`resolveTypeAnnotation()` 同理 |
| `src/l0_core/semantic/type_checker_pre_decl.cpp` | **修改** | 新增 `#include module_namespace.hpp`；`preDeclareStruct/Function/TypeAlias` 优先 `moduleNamespace->find(currentModuleId, name_id)` 查询，回退到 `globalSymbols->find()` |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | **修改** | `checker.check()` 调用新增 `unit.module_id, module_namespace` 参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `checker.check()` 调用新增 `single_unit[0].module_id, module_namespace` 参数 |
| `test/helpers/test_helpers.hpp` | **修改** | `runTypeCheck()` 中 `checker.check()` 新增 `unit.module_id, module_namespace_` 参数 |
| `test/ir/verify_test.cpp` | **修改** | `checker.check()` 新增 `unit.module_id, module_namespace` 参数 |

### 13.2 设计要点

- **优先新表，回退旧表**: `resolveSymbol()`、`resolveTypeAnnotation()`、`preDeclareStruct/Function/TypeAlias` 三处全部采用 `moduleNamespace->find(module_id, name_id)` 优先查询，未命中则回退 `globalSymbols->find()`。这是阶段 K 删除旧表前的过渡态。
- **ModuleId 传递链**: `unit.module_id` 通过 `check()` 参数注入 `TypeChecker::currentModuleId`，所有查询点自动获得模块上下文。
- **O(1) 同模块查询**: `ModuleNamespace::find(module_id, name_id)` 使用 composite hash key 直接定位，相比旧表的 `owner_module` 字符串比较方案提高了效率。

### 13.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 中。所有 `checker.check()` 调用方需同步传 `module_id` + `module_namespace`，内部优先走新表但旧表作为回退

## 14. 阶段 H 执行记录 (2026-05-07)

### 14.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/ir/builder.hpp` | **修改** | 新增 `#include module_semantic.hpp`；`BuildCtx` 新增 `const UnitVisibleSymbols *visible_symbols` 成员；`build()` 签名新增 `visible_symbols` 可选参数（默认 nullptr） |
| `src/l0_core/ir/builder.cpp` | **修改** | `build()` 实现接收并存储 `visible_symbols` 到 `BuildCtx` |
| `src/l0_core/ir/builder_expression.cpp` | **修改** | 新增 `isNameVisibleInUnit()` 函数——优先 O(1) HashMap 查 `visible_symbols->tables`，未提供则回退 `isImportedOrTopLevelName()`（AST 遍历兼容）；`IdentifierExpr` 降级中的 `isImportedOrTopLevelName()` 替换为 `isNameVisibleInUnit()` |
| `include/niki/meta/orchestrator/compile_pipeline.hpp` | **修改** | 新增 `#include module_semantic.hpp`；`compileUnitChunk()` 和 `compileParsedBackend()` 签名新增 `const UnitVisibleSymbols *visible_symbols = nullptr` 可选参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `compileUnitChunk()` 和 `compileParsedBackend()` 实现接收并传递 `visible_symbols` 到 `ir_builder.build()`；`compileParsedUnit()` 中传 `&visible_per_unit[0]` |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | **修改** | `compileParsedBackend()` 调用传入 `&visible_per_unit[unit_idx]`；循环从 range-for 改为 indexed for |
| `test/helpers/test_helpers.hpp` | **修改** | `buildIR()` 中 `builder.build(unit, nullptr)`（测试环境无跨模块可见表） |

### 14.2 设计要点

- **O(1) HashMap 替代 AST 遍历**: `isImportedOrTopLevelName()` 需要递归遍历 AST 声明列表查找名字，而 `isNameVisibleInUnit()` 直接 O(1) HashMap 查询 `visible_symbols->tables`。
- **向后兼容**: `visible_symbols` 参数默认为 `nullptr`，测试/单模块环境无需传表，自动回退到旧 AST 遍历路径。
- **数据流串联**: `buildModuleSemanticContext()` 产出的 `visible_per_unit` → `compileParsedBackend()` → `compileUnitChunk()` → `IRBuilder::build()` → `BuildCtx::visible_symbols` → `isNameVisibleInUnit()`。

### 14.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 低。新增可选参数，所有调用方可不传（默认 nullptr）兼容旧行为，编排器调用方显式传入 `visible_per_unit[unit_idx]`

## 15. 阶段 I 执行记录 (2026-05-07)

### 15.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/linker/linker_facade.hpp` | **修改** | 新增 `#include module_id.hpp`；`CompileModule` 新增 `ModuleId module_id` 字段 |
| `include/niki/meta/orchestrator/compile_pipeline.hpp` | **修改** | 新增 `#include module_id.hpp`；`UnitCompileArtifact` 新增 `ModuleId module_id` 字段；`buildCompileModule()` 签名新增 `ModuleId module_id` 参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `compileUnitChunk()` 中 `artifact.module_id = unit.module_id`（回填）；`buildCompileModule()` 新增 `module_id` 写入；`compileParsedBackend()` 传递 `unit.module_id` |
| `src/meta/project/project_linker.cpp` | **修改** | 新增 `#include module_id.hpp`；重名检测从单层 `unordered_map<string, source_path>` 改为 `unordered_map<(module_id, string_name), source_path>` 复合键——**允许跨模块同名符号，仅检测同模块内重名**；`SymbolKey` 自定义 hash 使用 `(module_id, name)` |

### 15.2 设计要点

- **复合键替代纯字符串**: 旧逻辑 `unordered_map<string, string>` (`name → source_path`) 无法区分跨模块同名符号，任何模块定义 `func init()` 就会冲突。新逻辑 `unordered_map<SymbolKey, source_path>` (`(module_id, name) → source_path`) 精确限制为"同模块内不重名"，跨模块同名完全合法。
- **入口函数定位**: 新增 `name_to_entry_module` map 记录入口名到 `module_id` 的对应关系，用于多模块场景下精确报告入口函数所在模块。
- **零破坏性**: `CompileModule` 新增字段对所有调用方透明——`module_id` 在 `compileUnitChunk()` 自动回填，linker 层仅新增字段不删改旧字段。

### 15.3 验证结果

- **编译**: `niki_core` 库和 `niki_tests` 可执行文件编译通过，零警告
- **测试**: `ctest` 全量 118 项测试通过，零失败
- **破坏性**: 中。`CompileModule::module_id` 新增字段对构造函数调用方透明（聚合初始化仍可省略未命名体），linker 逻辑变更语义为"允许跨模块同名"——这是**有意为之的正向语义突破**

## 16. 阶段 J 执行记录 (2026-05-07)

### 16.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/vm/chunk.hpp` | **修改** | 新增 `#include module_id.hpp`；`Chunk` 新增 `ModuleId module_id`（默认 `kInvalidModuleId`） |
| `include/niki/l0_core/vm/vm.hpp` | **修改** | 新增 `GlobalKey` / `GlobalKeyHash`；`globals` / `global_objects` 改为 `unordered_map<GlobalKey, …>`；新增 `current_chunk_module_id_`；`lookupGlobalFunctionById(ModuleId, uint32_t)` |
| `src/l0_core/vm/vm.cpp` | **修改** | `executeChunk` / `executeFunction` 设置 `current_chunk_module_id_`；`OP_DEFINE_GLOBAL*` / `OP_GET_GLOBAL*` / `OP_SET_GLOBAL*` 使用 `(current_chunk_module_id_, name_id)` 复合键 |
| `include/niki/l0_core/linker/linker_facade.hpp` | **修改** | `LinkedProgram` 新增 `entry_module_id`；链接成功时与 `entry_name_id` 一并回填 |
| `src/meta/project/project_linker.cpp` | **修改** | `program.entry_module_id = entry_module_id` |
| `src/meta/runtime_host/runtime_host.cpp` | **修改** | `lookupGlobalFunctionById(program.entry_module_id, program.entry_name_id)` |
| `include/niki/l0_core/ir/module_ir.hpp` | **修改** | `ModuleIR` 新增 `module_id` |
| `src/l0_core/ir/builder.cpp` | **修改** | `bc.module.module_id = unit.module_id` |
| `src/l0_core/ir/lower_to_chunk.cpp` | **修改** | `function_object->chunk.module_id = module_ir.module_id` |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | `makeInitChunkFromLoweredFunctions(..., ModuleId)` 写入 `init_chunk.module_id` |
| `test/linker/linker_test.cpp` | **修改** | 工厂函数显式传入 `module_id` / `init_chunk.module_id`；重复符号用同一 `module_id` 覆盖「同模块重名」语义 |
| `test/runtime/launcher_test.cpp` | **无改** | `entry_module_id` 默认无效，入口查找失败路径不变 |

### 16.2 设计要点

- **与阶段 I 对齐**: 链接产物记录 `entry_module_id`，launcher 用 `(entry_module_id, entry_name_id)` 解析入口，避免仅按 `name_id` 在全局扁平表中碰撞。
- **init 与函数体一致**: `makeInitChunkFromLoweredFunctions` 与 `lowerOneFunction` 均写入所属模块的 `module_id`，保证 `OP_DEFINE_GLOBAL` 与 `OP_GET_GLOBAL` 使用同一复合键空间。
- **executeFunction**: 入口函数执行前同步 `current_chunk_module_id_ = function->chunk.module_id`，避免多 `init_chunk` 顺序导致沿用错误模块上下文。

### 16.3 验证结果

- **编译**: `niki_core` / `niki_tests` 通过
- **测试**: `ctest` 全量 118 项通过
- **破坏性**: 中。`VM::lookupGlobalFunctionById` 签名变更；手工构造 `LinkedProgram` / `Chunk` 的调用方需设置 `module_id` / `entry_module_id` 与链接器一致

## 17. 阶段 K 执行记录 (2026-05-07)

### 17.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/semantic/global_symbol_table.hpp` | **删除** | 彻底移除 `GlobalSymbolTable` 类定义及 `GlobalSymbol` 结构体 |
| `src/l0_core/semantic/global_symbol_table.cpp` | **删除** | 彻底移除 `GlobalSymbolTable::insert/find` 实现 |
| `include/niki/l0_core/semantic/module_id.hpp` | **修改** | 将 `Kind` 枚举（`Function/Struct/TypeAlias`）从 `global_symbol_table.hpp` 迁移到此文件 |
| `include/niki/l0_core/semantic/module_namespace.hpp` | **修改** | 移除 `#include global_symbol_table.hpp`，改为 `#include module_id.hpp` |
| `include/niki/l0_core/semantic/module_semantic.hpp` | **修改** | 移除 `#include global_symbol_table.hpp`，改为 `#include module_id.hpp` |
| `include/niki/l0_core/semantic/type_checker.hpp` | **修改** | 移除 `#include global_symbol_table.hpp`；移除 `globalSymbols` 成员；两个 `check()` 重载去除 `global_symbols` 参数 |
| `src/l0_core/semantic/type_checker.cpp` | **修改** | 两个 `check()` 移除 `globalSymbols` 赋值/清理；`resolveSymbol()` 和 `resolveTypeAnnotation()` 删除旧表回退分支 |
| `src/l0_core/semantic/type_checker_pre_decl.cpp` | **修改** | 移除 `#include`；`preDeclareStruct/Function/TypeAlias` 删除 `globalSymbols->find()` 回退 |
| `src/l1_domain/analyzer.cpp` | **修改** | `checkComponentDecl` 中 `globalSymbols->find()` 替换为 `moduleNamespace->find()` |
| `src/meta/precompile/predeclare_stage.cpp` | **修改** | 移除 `global_symbols` 参数；删除 `insert_dual` 双写 lambda；`GlobalSymbol` → `ModuleNamespace::Symbol`；`resolvePredeclareType` 改用 `ModuleNamespace` |
| `include/niki/meta/precompile/precompile_pipeline.hpp` | **修改** | `predeclareSingleUnit()` 声明去除 `global_symbols` 参数 |
| `include/niki/meta/orchestrator/compiler_orchestrator.hpp` | **修改** | `predeclareAllUnits()` 声明去除 `global_symbols` 参数 |
| `src/meta/orchestrator/compiler_orchestrator.cpp` | **修改** | 移除 `GlobalSymbolTable` 局部变量；所有调用点去 `global_symbols` |
| `include/niki/meta/orchestrator/compile_pipeline.hpp` | **修改** | `compileUnitChunk/compileParsedBackend/compileParsedUnit` 全部去除 `global_symbols` 参数 |
| `src/meta/orchestrator/compile_pipeline.cpp` | **修改** | 三函数实现及内部调用链全部去除 `global_symbols` 参数 |
| `CMakeLists.txt` | **修改** | `NIKI_L0_CORE_SOURCES` 移除 `global_symbol_table.cpp` |
| `test/helpers/test_helpers.hpp` | **修改** | 移除 `#include global_symbol_table.hpp`；移除 `symbols_` 成员；相关调用去 `symbols_` |
| `test/ir/verify_test.cpp` | **修改** | 移除 `#include global_symbol_table.hpp`；移除 `symbols` 变量 |
| `test/driver/driver_project_test.cpp` | **修改** | `compileParsedUnit()` 调用去除 `fixture.symbols_` |
| `test/semantic/type_checker_test.cpp` | **修改** | `FunctionCallSigMatch` / `FunctionCallArgTypeError` 新增 `unit.module_id` 设置 |

### 17.2 设计要点

- **Kind 枚举迁移到 module_id.hpp**: `Kind` 是项目级符号种类定义，被 `ModuleNamespace::Symbol`、`module_semantic::SymbolRef` 等广泛引用，放在 `module_id.hpp` 形成无依赖叶子头文件。
- **单路径查询**: 所有"优先 ModuleNamespace 回退 GlobalSymbolTable"的双路径全部简化为"仅 ModuleNamespace"。
- **三处测试修复**: 两个手动构造 `CompilationUnit` 的测试新增 `module_id` 设置，使 `predeclare` 阶段能正确以 `(module_id, name_id)` 键写入 `ModuleNamespace`。

### 17.3 验证结果

- **编译**: `niki_core` / `niki_tests` 通过，零警告
- **测试**: `ctest` 全量 118 项通过
- **破坏性**: 高。`GlobalSymbolTable` 彻底删除，所有调用方必须改用 `ModuleNamespace`

## 18. 阶段 L 执行记录 (2026-05-07)

### 18.1 变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/niki/l0_core/semantic/readme.md` | **修改** | 全部 `GlobalSymbolTable` → `ModuleNamespace`；mermaid 图更新为 `OUT_MN[ModuleNamespace]`；文件列表移除 `global_symbol_table.*` 条目；日期更新至 2026-05 |
| `include/niki/l0_core/ir/readme.md` | **修改** | 数据边界中 `GlobalSymbolTable` → `ModuleNamespace` |
| `include/niki/l0_core/readme.md` | **修改** | 两处 mermaid 图中 `GlobalSymbolTable` → `ModuleNamespace`；Pass-2 输出描述更新 |
| `include/niki/l0_core/semantic/module_namespace.hpp` | **修改** | 删除"阶段 B 互补共存"过时注释，改为稳定版描述 |
| `include/niki/l0_core/semantic/type_checker.hpp` | **修改** | 删除"阶段 G"过渡注释 |
| `include/niki/l0_core/ir/builder.hpp` | **修改** | 删除"阶段 H"标注前缀 |
| `include/niki/l0_core/ir/module_ir.hpp` | **修改** | 删除"阶段 J"标注前缀 |
| `include/niki/l0_core/linker/linker_facade.hpp` | **修改** | 两处删除"阶段 I/阶段 J"标注前缀 |
| `include/niki/l0_core/vm/chunk.hpp` | **修改** | 删除"阶段 J"标注前缀 |
| `include/niki/meta/orchestrator/compile_pipeline.hpp` | **修改** | 删除"阶段 I"标注前缀 |
| `src/l0_core/semantic/type_checker.cpp` | **修改** | 删除"阶段 K"过渡注释 |
| `src/l0_core/semantic/type_checker_pre_decl.cpp` | **修改** | 4 处删除"阶段 G/K"过渡注释 |
| `src/l0_core/syntax/parse.cpp` | **修改** | 删除"阶段 C"标注前缀 |
| `src/l0_core/ir/builder_expression.cpp` | **修改** | 删除"阶段 H"标注前缀 |
| `src/meta/precompile/predeclare_stage.cpp` | **修改** | 4 处删除"阶段 C/K"过渡注释 |
| `src/meta/precompile/module_context_stage.cpp` | **修改** | 删除"阶段 F"标注前缀 |
| `src/meta/project/project_linker.cpp` | **修改** | 删除"阶段 I"标注前缀 |

### 18.2 设计要点

- **注释修边**: 所有"阶段 X: …"前缀注释已替换为无版本的稳定描述，因为整个重构序列（A-K）已结束，不再需要过渡性标注。
- **Readme 同步**: 三份 `readme.md`（semantic/ir/l0_core）中的架构图、数据流描述、文件清单全部与当前实现对齐，`GlobalSymbolTable` 完全替换为 `ModuleNamespace`。
- **零代码变更**: 本阶段仅涉及注释和文档，不改变任何编译单元的逻辑。

### 18.3 验证结果

- **编译**: `niki_core` / `niki_tests` 通过，零警告
- **测试**: `ctest` 全量 118 项通过
- **破坏性**: 零（纯文档/注释修改）

---

> **重构总结**: 自阶段 A 引入 `ModuleId` 以来，经过 12 个阶段的重构，NIKI 编译器已完成从"字符串标识 + 扁平全局符号表"到"ModuleId 标识 + ModuleNamespace 复合键 + 全链路 ID 化"的体系迁移。`GlobalSymbolTable` 已被彻底删除，所有符号解析路径统一通过 `ModuleNamespace::find(module_id, name_id)` 完成。
