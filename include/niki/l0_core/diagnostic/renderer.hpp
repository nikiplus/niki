#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include <string>

/** @renderer: 诊断结果渲染接口
 * `diagnostic.hpp` 解决的是“错误如何表示与聚合”，而这个头文件解决的是“聚合后的错误如何输出”。
 * 在工程上，这两者必须分离：数据结构要稳定可计算，输出格式要可替换可演进。
 *
 * 渲染接口同时提供 text 与 json 两类输出，背后是两种消费场景：
 * - text：面向开发者终端与本地调试，强调可读性与定位速度；
 * - json：面向工具链/IDE/CI 机器消费，强调结构化与可解析性。
 * 如果只保留文本输出，自动化系统很难做稳定处理；如果只保留 JSON，人类排错体验会显著下降。
 * 双轨输出是现实工程中的必要折中。
 *
 * 注意这里仍然只声明接口，不携带渲染策略实现细节。
 * 这样可以保证上层模块只依赖“可渲染”能力，而不被具体格式细节绑定；
 * 将来无论是增加颜色高亮、源码片段、国际化文案，还是扩展更严格 JSON schema，
 * 都可以在实现层迭代而不破坏调用方依赖。
 */
namespace niki::diagnostic {

std::string renderDiagnosticText(const Diagnostic &diagnostic);
std::string renderDiagnosticBagText(const DiagnosticBag &bag);
std::string renderDiagnosticJson(const Diagnostic &diagnostic);
std::string renderDiagnosticBagJson(const DiagnosticBag &bag);

} // namespace niki::diagnostic
