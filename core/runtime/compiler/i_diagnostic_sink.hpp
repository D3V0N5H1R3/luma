// ─────────────────────────────────────────────────────────────────────────────
// IDiagnosticSink — Narrow interface for compiler diagnostics
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Report compilation errors and warnings, including the
//   specialised "limit exceeded" diagnostic.
//
// Part of the ICompilationBackend interface-segregation (ISP) split.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_DIAGNOSTIC_SINK_HPP
#define LUMA_COMPILER_I_DIAGNOSTIC_SINK_HPP

#include <cstddef>
#include <string_view>

#include "analysis/source/source_location.hpp"

namespace luma {

// Compiler diagnostic reporting surface.
class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;

    virtual void error(std::string_view message, SourceLocation loc,
                       std::string_view hint = "") = 0;
    virtual void warning(std::string_view message, SourceLocation loc,
                         std::string_view hint = "") = 0;
    virtual void error_limit_exceeded(std::string_view description, std::size_t maximum,
                                      SourceLocation loc, std::string_view hint) = 0;

protected:
    IDiagnosticSink() = default;
    IDiagnosticSink(const IDiagnosticSink&) = default;
    IDiagnosticSink& operator=(const IDiagnosticSink&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_DIAGNOSTIC_SINK_HPP
