#include "niki/l0_core/diagnostic/diagnostic.hpp"
#include <gtest/gtest.h>

using namespace niki::diagnostic;

TEST(DiagnosticEmitTest, ScannerErrorMapsStageSeverityAndCode) {
    DiagnosticBag bag;
    bag.error(events::ScannerCode::InvalidToken, "bad token", makeSourceSpan("a.nk", 3, 5, 1));

    ASSERT_EQ(bag.size(), 1U);
    const auto &diagnostic = bag.all()[0];
    EXPECT_EQ(diagnostic.stage, DiagnosticStage::Scanner);
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::Error);
    EXPECT_EQ(diagnostic.code, codeOf(events::ScannerCode::InvalidToken));
    EXPECT_EQ(diagnostic.message, "bad token");
    EXPECT_EQ(diagnostic.span.file, "a.nk");
    EXPECT_EQ(diagnostic.span.line, 3U);
    EXPECT_EQ(diagnostic.span.column, 5U);
    EXPECT_EQ(diagnostic.span.length, 1U);
}

TEST(DiagnosticEmitTest, LinkerWarningMapsStageSeverityAndCode) {
    DiagnosticBag bag;
    bag.warning(events::LinkerCode::DuplicateSymbol, "dup");

    ASSERT_EQ(bag.size(), 1U);
    const auto &diagnostic = bag.all()[0];
    EXPECT_EQ(diagnostic.stage, DiagnosticStage::Linker);
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::Warning);
    EXPECT_EQ(diagnostic.code, codeOf(events::LinkerCode::DuplicateSymbol));
    EXPECT_EQ(diagnostic.message, "dup");
}

TEST(DiagnosticEmitTest, DriverInfoMapsStageSeverityAndCode) {
    DiagnosticBag bag;
    bag.info(events::DriverCode::NoInput, "no input");

    ASSERT_EQ(bag.size(), 1U);
    const auto &diagnostic = bag.all()[0];
    EXPECT_EQ(diagnostic.stage, DiagnosticStage::Driver);
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::Info);
    EXPECT_EQ(diagnostic.code, codeOf(events::DriverCode::NoInput));
    EXPECT_EQ(diagnostic.message, "no input");
}

TEST(DiagnosticEmitTest, EmitNormalizesUnknownFileForPositionedSpan) {
    DiagnosticBag bag;
    bag.error(events::SemanticCode::GenericError, "semantic fail", makeSourceSpan("", 10, 2));

    ASSERT_EQ(bag.size(), 1U);
    const auto &diagnostic = bag.all()[0];
    EXPECT_EQ(diagnostic.stage, DiagnosticStage::Semantic);
    EXPECT_EQ(diagnostic.code, codeOf(events::SemanticCode::GenericError));
    EXPECT_EQ(diagnostic.span.file, "<unknown>");
    EXPECT_EQ(diagnostic.span.line, 10U);
    EXPECT_EQ(diagnostic.span.column, 2U);
}
