# Niki L0 Core 总览

`l0_core` 是 Niki 的编译-链接-运行基础层。  
本文件仅提供：

- 顶层模块关系
- 端到端数据流模型
- 各子模块文档索引

## 子模块文档

- `l0_core/syntax/readme.md`
- `l0_core/ir/readme.md`
- `l0_core/semantic/readme.md`
- `l0_core/linker/readme.md`
- `l0_core/runtime/readme.md`
- `l0_core/vm/readme.md`
- `l0_core/diagnostic/readme.md`

## 顶层模块关系

```mermaid
graph TD
    D[driver] --> SX[syntax]
    SX --> SM[semantic]
    SM --> IR[ir pipeline]
    IR --> LK[linker]
    LK --> RT[runtime]
    RT --> VM[vm]

    DG[diagnostic] --> SX
    DG --> SM
    DG --> IR
    DG --> LK
    DG --> RT
    DG --> VM
```

## 数据流转模型

```mermaid
graph LR
    S[source text] --> T[tokens]
    T --> A[ASTPool]

    A --> P[Predeclare]
    P --> G1[GlobalSymbolTable]
    P --> G2[GlobalTypeArena]

    A --> TC[TypeCheck]
    G1 --> TC
    G2 --> TC
    TC --> NT[node_types]

    A --> IRB[IRBuilder]
    NT --> IRB
    G1 --> IRB
    G2 --> IRB
    IRB --> VRF[Verify]
    VRF --> LWC[LowerToChunk]
    LWC --> CH[Chunk]

    CH --> CM[CompileModule]
    CM --> L[LinkedProgram]
    L --> R[Runtime Launcher]
    R --> V[VM Execution]
    V --> O[vm::Value]
```

## 五条主链路（图）

### 1) 编译执行主链

```mermaid
graph LR
    S[source] --> SC[Scanner]
    SC --> PR[Parser]
    PR --> PD[Predeclare]
    PD --> TC[TypeCheck]
    TC --> IRP[IRBuilder + Verify + Lower]
    IRP --> LK[Linker]
    LK --> RT[Runtime]
    RT --> VM[VM]
    VM --> O[vm::Value]
```

### 2) 语义分析链

```mermaid
graph LR
    A[ASTPool] --> PD[Predeclare]
    PD --> GST[GlobalSymbolTable]
    PD --> GTA[GlobalTypeArena]
    A --> TC[TypeChecker]
    GST --> TC
    GTA --> TC
    TC --> NT[ASTPool.node_types]
```

### 3) 报错诊断链

```mermaid
graph LR
    SX[syntax] --> RP[Report]
    SM[semantic] --> RP
    LK[linker] --> RP
    RT[runtime] --> RP
    VM[vm] --> RP
    RP --> BAG[DiagnosticBag]
    BAG --> MG[merge]
    MG --> RN[renderer]
    RN --> OUT[diagnostic output]
```

### 4) 链接装载链

```mermaid
graph LR
    CM[CompileModule[]] --> LK[Linker]
    LK --> LP[LinkedProgram]
    LP --> LA[Launcher]
    LA --> VM[VM ready state]
```

### 5) 运行时执行链

```mermaid
graph LR
    EN[entry function/chunk] --> FD[Fetch]
    FD --> DC[Decode]
    DC --> EX[Execute]
    EX --> ST[State Update]
    ST --> FD
    EX --> RES[Value / InterpretResult]
```

## 当前实现结构（2026-04）

- 编译主链路以 IR 管线为唯一实现路径：
  - `Scanner -> Parser -> Predeclare -> TypeCheck -> IRBuilder -> Verify -> LowerToChunk -> Linker -> Launcher -> VM`
- `syntax` 只负责前端（Token/AST），不再承担字节码生成。
- `ir` 负责中间表示构建、校验与降级到 `vm::Chunk`。
- `linker + runtime + vm` 负责装载与执行阶段。
- `diagnostic` 作为跨阶段统一诊断通道。

## 阶段索引

- Pass-1 Parse
  - 输入：`source text`
  - 输出：`GlobalCompilationUnit{tokens, ASTPool, root}`
  - 细节：`syntax/readme.md`
- Pass-2 Predeclare
  - 输入：全部 Unit AST
  - 输出：`GlobalSymbolTable` + `GlobalTypeArena`
  - 细节：`semantic/readme.md`
- Pass-3 TypeCheck
  - 输入：`ASTPool` + 全局语义表
  - 输出：`ASTPool.node_types`
  - 细节：`semantic/readme.md`
- Pass-4 Build IR + Lower
  - 输入：`ASTPool` + `node_types` + 全局语义表
  - 输出：`ModuleIR` -> `Chunk` / `CompileModule`
  - 细节：`ir/readme.md`
- Link + Run
  - 输入：模块集合
  - 输出：`vm::Value`
  - 细节：`linker/readme.md`、`runtime/readme.md`、`vm/readme.md`