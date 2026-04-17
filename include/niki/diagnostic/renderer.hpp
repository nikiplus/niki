#pragma once

#include "niki/diagnostic/diagnostic.hpp"
#include <string>

namespace niki::diagnostic {

std::string renderDiagnosticText(const Diagnostic &diagnostic);
std::string renderDiagnosticBagText(const DiagnosticBag &bag);

} // namespace niki::diagnostic
