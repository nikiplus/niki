#pragma once

#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include <string>

namespace niki::diagnostic {

std::string renderDiagnosticText(const Diagnostic &diagnostic);
std::string renderDiagnosticBagText(const DiagnosticBag &bag);
std::string renderDiagnosticJson(const Diagnostic &diagnostic);
std::string renderDiagnosticBagJson(const DiagnosticBag &bag);

} // namespace niki::diagnostic
