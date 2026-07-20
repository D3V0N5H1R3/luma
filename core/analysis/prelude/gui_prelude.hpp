#ifndef LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_HPP
#define LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_HPP

// The built-in Solaris surface — Luma's beginner-first GUI framework — shipped
// inside the binary rather than as an includable
// library.  It is ordinary Luma source, embedded here as a string and injected
// into any program that references Solaris, before type checking.
//
// The prelude provides:
//   * global design-token `choice` types (Emphasis, TextScale, Weight, Spacing,
//     Align, Justify, Length, Radius, Scheme) and the immutable `View` record —
//     kept top-level because the type checker cannot resolve a namespaced record
//     in a `with`-update nor destructure a namespaced choice data-variant; and
//   * `namespace Solaris` — element constructors, fluent modifiers, the
//     internal reconciler onto the low-level GraphicalUi module, and the app
//     builder/runner.
//
// Injection is conditional (only when the source mentions Solaris) so
// non-GUI programs pay nothing and get no global-type pollution, and it is
// guarded against double-definition.

#include <string>
#include <string_view>

#include "analysis/source/source_location.hpp"

namespace luma {

struct Program;
class SourceManager;

namespace prelude {

// The embedded Luma source of the Solaris surface.
[[nodiscard]] const std::string& gui_prelude_source();

// True when `source` references the Solaris surface and therefore needs the
// prelude.  Matches the trigger name only as a standalone identifier (not inside
// a longer name such as `mySolarisHelper`).
[[nodiscard]] bool source_uses_gui(std::string_view source);

// Parse the embedded prelude and prepend its declarations to `program`, giving
// them a virtual file in `source_manager` (named "<gui-prelude>") so their
// source locations never shift user line numbers and any prelude diagnostic
// still renders with context.  A no-op when `program` already declares
// `namespace Solaris` (guards against double injection and lets a program
// provide its own surface).
void inject_gui_prelude(Program& program, SourceManager& source_manager);

// As `inject_gui_prelude`, but tags the prelude's declarations with the caller-
// supplied `prelude_file_id` and registers no virtual source file.  For callers
// (such as the language server) that manage their own file-id space, do not use
// a shared SourceManager, and render diagnostics by line against the user's own
// document: because the prelude's source text is not retained, a prelude
// location cannot be rendered and would otherwise paint onto the wrong line of
// the user's file, so such callers MUST allocate a `prelude_file_id` distinct
// from every user/include file id and drop any diagnostic carrying it.  Returns
// true when the prelude was injected, or false when `program` already declares
// `namespace Solaris` or the embedded prelude fails to lex or parse.
[[nodiscard]] bool inject_gui_prelude_tagged(Program& program, FileId prelude_file_id);

} // namespace prelude
} // namespace luma

#endif // LUMA_ANALYSIS_PRELUDE_GUI_PRELUDE_HPP
