#pragma once

#include "niki/diagnostic/diagnostic.hpp"

namespace niki::debug {

void logDiagnostic(const niki::diagnostic::Diagnostic &diagnostic);
void logDiagnosticBag(const niki::diagnostic::DiagnosticBag &bag);

} // namespace niki::debug
