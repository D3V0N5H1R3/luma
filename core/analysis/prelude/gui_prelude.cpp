#include "analysis/prelude/gui_prelude.hpp"

#include <cassert>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/prelude/gui_prelude_generated.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/string_utils.hpp"

namespace luma::prelude {

namespace {

// The trigger token: any program that mentions this name gets the prelude.
constexpr std::string_view k_trigger = "Solaris";

// The virtual source name under which the prelude is registered.  Diagnostics
// raised against prelude code attribute to this file rather than a user file.
constexpr std::string_view k_prelude_name = "<gui-prelude>";

// True when the program already declares `namespace Solaris` — either
// because the prelude was already injected on an earlier pass or because the
// program supplies its own surface.  Prevents duplicate declarations.
[[nodiscard]] bool program_declares_gui(const Program& program) {
    for (const auto& decl : program.declarations) {
        if (decl && decl->kind == DeclarationKind::Namespace) {
            const auto& ns = static_cast<const NamespaceDeclaration&>(*decl);

            if (ns.name == k_trigger) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

const std::string& gui_prelude_source() {
    // The Solaris surface is ordinary Luma, authored in gui_prelude.luma and
    // embedded verbatim by scripts/generate_prelude_asset.mjs (see
    // gui_prelude_generated.hpp).  Design tokens and the `View` record are
    // top-level (the type checker cannot handle a namespaced record in a
    // `with`-update nor destructure a namespaced choice data-variant); the
    // framework itself lives under `namespace Solaris` and reconciles onto the
    // low-level `GraphicalUi` web-view module.
    static const std::string source{k_gui_prelude_source};

    return source;
}

bool source_uses_gui(std::string_view source) {
    return contains_identifier_token(source, k_trigger);
}

namespace {

// Lex, parse, and prepend the embedded Solaris prelude, tagging every prelude
// token — and thus every prelude AST location and diagnostic — with `file_id`.
// Returns true when the prelude was injected, false when `program` already
// declares `namespace Solaris` or the embedded prelude fails to lex or parse.
//
// The prelude is developer-controlled and covered by tests, so a lex or parse
// failure is a build-time defect: the assert makes that loud in debug and test
// builds.  The matching runtime guard keeps release builds *safe* — a malformed
// prelude prepends nothing, rather than a half-parsed, corrupt set of
// declarations that would resurface as baffling errors in the user's own code.
[[nodiscard]] bool prepend_prelude(Program& program, FileId file_id) {
    if (program_declares_gui(program)) {
        return false;
    }

    DiagnosticCollector diagnostics;
    Lexer lexer{gui_prelude_source(), diagnostics, file_id};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    Program prelude = parser.parse();

    const bool prelude_ok = !diagnostics.has_errors() && parser.get_errors().empty();
    assert(prelude_ok && "Solaris prelude failed to lex or parse");
    if (!prelude_ok) {
        return false;
    }

    // Prepend so the prelude's tokens and record are declared before user code.
    auto& destination = program.declarations;
    destination.insert(destination.begin(), std::make_move_iterator(prelude.declarations.begin()),
                       std::make_move_iterator(prelude.declarations.end()));
    return true;
}

} // namespace

void inject_gui_prelude(Program& program, SourceManager& source_manager) {
    if (program_declares_gui(program)) {
        return;
    }

    // Register the prelude as a virtual source so its AST locations attribute to
    // "<gui-prelude>" and never shift the user's line numbers, and so any prelude
    // diagnostic still renders with source context under this SourceManager.
    const SourceFile& prelude_file =
        source_manager.load_virtual(k_prelude_name, gui_prelude_source());

    (void)prepend_prelude(program, prelude_file.file_id);
}

bool inject_gui_prelude_tagged(Program& program, FileId prelude_file_id) {
    return prepend_prelude(program, prelude_file_id);
}

} // namespace luma::prelude
