# Niki 诊断系统规范

本文档规定 Niki 全链路错误与日志策略，适用于 `scanner/parser/semantic/compiler/linker/launcher/driver`。

## 1. 错误码常量统一

- 所有 `Diagnostic.code` 必须来自 `include/niki/diagnostic/codes.hpp`。
- 禁止在业务代码里手写错误码字符串字面量（测试断言可引用常量）。
- 新增错误码时按 stage 分组追加到 `codes.hpp`，命名使用 `UPPER_SNAKE_CASE`。

## 2. SourceSpan 填充策略统一

- 最小必填：`stage + code + message`。
- 推荐填充：`file + line + column + length`。
- 当存在 `line/column` 但无 `file` 时，`DiagnosticBag::report` 会自动回填 `\"<unknown>\"`。
- 所有阶段优先使用 `makeSourceSpan(...)` 构造 span。

## 3. reportError/reportWarning/reportInfo 统一

- 统一使用 `DiagnosticBag::reportError/reportWarning/reportInfo` 作为接口。
- `addError/addWarning/addInfo` 保留为兼容别名，但新代码不应优先使用。

## 4. CLI 输出策略统一

- CLI 支持：
  - `--diagnostic-format=text`
  - `--diagnostic-format=json`
- `text/json` 必须包含一致字段：
  - `severity`
  - `stage`
  - `code`
  - `message`
  - `span.file`
  - `span.line`
  - `span.column`
  - `span.length`
  - `notes`

## 5. 返回类型风格统一

- 公共“可失败”接口统一返回：
  - `std::expected<T, niki::diagnostic::DiagnosticBag>`
- 内部纯工具函数可使用普通返回值，不引入 `expected`。
- 新增 API 不允许再定义阶段私有 `*Error` 聚合结构体作为主错误通道。

## 6. 日志调用层级统一

- 业务层负责产出 `Diagnostic`，不负责错误日志落盘。
- 日志写入由适配层统一处理（例如 `debug/diagnostic_logger.*`）。
- 禁止直接使用 `spdlog::error` 在业务模块中输出错误。

## 7. 新增模块接入清单

新增可失败模块时，必须同时完成：

1. 在 `DiagnosticStage` 增加新 stage（如有必要）
2. 在 `codes.hpp` 增加错误码分组
3. 在模块内使用 `DiagnosticBag::report*` 报错
4. 在测试中按 `Diagnostic.code` 断言
5. 通过 `text/json` 渲染检查输出一致性
