---
description: "Diagnose and fix a bug in the Luma language server (LSP)"
agent: "agent"
argument-hint: "Bug description, e.g. 'hover shows the wrong range on strings containing emoji'"
version: 1
lastUpdated: "2026-08-01"
---

# Bug Fix — Language Server

Diagnose and fix a bug in the Luma language server (`luma_lsp`). The server reuses the interpreter front-end (lexer, parser, type checker) but never runs the VM, so most bugs are wrong diagnostics, hover, completion, navigation, or coordinate conversion. Follow a structured approach:

1. **Reproduce** the bug with a minimal Luma document and the specific LSP request that misbehaves (diagnostics, hover, completion, signature help, go-to-definition, references, rename, semantic tokens, code actions, code lens, formatting, folding, inlay hints, call/type hierarchy, linked editing). Confirm the failure, ideally as a C++ test in `language-server/tests/`.
2. **Isolate the layer** where the bug occurs by tracing through the server:
    - Protocol transport (`lsp_transport.hpp`, `lsp_transport_wrapper.*`, `shared/protocol/`) — Content-Length framing or JSON-RPC parsing wrong?
    - Server dispatch (`lsp_server.*`, `lsp_server_dispatch.cpp`, `lsp_handler_registry.hpp`) — request routed to the wrong handler, or capability not advertised?
    - Analysis pipeline (`lsp_analysis_service_impl.*`, `lsp_analysis_pipeline.*`, `lsp_analysis_cache.*`, `lsp_cancellation_manager.hpp`) — front-end reuse, caching, or cancellation wrong?
    - Analysis result indexing (`lsp_analysis_result.hpp`, `lsp_token_index.hpp`, `lsp_scope_stack.*`, `lsp_identifier_collector.*`) — token index, function body ranges, scoped locals, or call graph wrong?
    - Coordinate conversion (`lsp_token_utils.*`, `lsp_position_utils.hpp`) — 1-based source vs 0-based LSP positions, or UTF-16 vs codepoint columns wrong?
    - Diagnostic building (`lsp_diagnostic_builder.*`, `lsp_diagnostic_codes.hpp`) — UTF-16 conversion, severity, or documentation URL wrong?
    - Symbol resolution (`lsp_symbol_resolver.*`, `lsp_definition_resolver.hpp`, `lsp_symbol_lookup.hpp`, `lsp_navigation_handler.hpp`, `lsp_rename_handler.hpp`) — scope-aware lookup backing hover, definition, references, rename, document highlight, and linked editing wrong?
    - Completion & signature help (`lsp_completion_handler.hpp`, `lsp_completion_provider.*`, `lsp_completion_strategy.*`, `lsp_keyword_catalog.*`, `lsp_stdlib_registry.*`, `lsp_server_signature.cpp`) — wrong or missing suggestions, wrong sort/filter text, or wrong signature help?
    - Include handling (`lsp_include_processor.*`) — cross-file resolution wrong?
    - Workspace indexing (`lsp_workspace_manager.*`, `lsp_workspace_indexer.*`, `lsp_persisted_index.*`) — stale or missing cross-file symbols?
    - Configuration (`lsp_configuration_manager.*`, `lsp_config.hpp`, `lsp_capabilities.*`) — client capability negotiation or settings wrong?
    - Document sync (`lsp_document_store.*`, `lsp_document_synchronizer.*`, `lsp_sync_handler.hpp`) — ranged incremental edit applied incorrectly?
    - Hover & type rendering (`lsp_hover_handler.hpp`, `lsp_hover_literals.*`, `lsp_type_formatter.hpp`, `lsp_server_hover.cpp`) — correct symbol resolved but the rendered hover text or type/signature string wrong?
    - Semantic tokens (`lsp_semantic_tokens_handler.hpp`, `lsp_token_classifier.hpp`, `lsp_semantic_token_cache.hpp`, `lsp_server_semantic_tokens.cpp`) — wrong token type/modifier classification or stale token cache?
    - Code actions, quick fixes, refactoring & code lens (`lsp_code_action_handler.hpp`, `lsp_code_action_builder.hpp`, `lsp_quickfix_handler.hpp`, `lsp_refactoring_provider.hpp`, `lsp_server_code_actions*.cpp`) — wrong or missing quick fix, refactoring, or code-lens edit?
    - Formatting (`lsp_formatting_handler.hpp`, `lsp_server_formatting.cpp`, `lsp_text_formatter.hpp/cpp`) — wrong formatting edits or ranges (a standalone line/token formatter, intentionally separate from the core AST formatter so it degrades gracefully on incomplete code)?
    - Document & workspace symbols and hierarchy (`lsp_symbol_handler.hpp`, `lsp_hierarchy_handler.hpp`, `lsp_server_symbols.cpp`, `lsp_server_hierarchy.cpp`) — wrong outline, symbol kind, declaration range, or call/type hierarchy (the hierarchy relies on the analysis call graph)?
    - Folding, inlay hints, document links & selection ranges (`lsp_folding_handler.hpp`, `lsp_server_folding.cpp`, `lsp_inlay_hint_handler.hpp`, `lsp_server_inlay.cpp`, `lsp_navigation_handler.hpp`, `lsp_server_navigation.cpp`, `lsp_brace_matcher.hpp`) — wrong ranges or hint placement?
    - The feature handlers themselves: each `lsp_*_handler.hpp` declares a request's entry point, the matching `lsp_server_*.cpp` implements it, and `lsp_server_dispatch.cpp` routes the request to it.
3. Read [Luma_Language_Server.md](../../documents/Luma_Language_Server.md) and the relevant source files to understand the current behaviour.
4. **Fix** the root cause with the smallest correct change. If the root cause lies in the shared interpreter front-end (lexer, parser, or type checker) rather than the server's own code, fix it there via [bug-fix.prompt.md](bug-fix.prompt.md). Watch for the most common LSP pitfall: source columns and LSP `character` positions are codepoint-based, never byte-based — use `lexeme_column_width()` (`lsp_token_utils.hpp`), never `lexeme.size()`, for any column or range width.
5. **Add a regression test** in the appropriate `language-server/tests/lsp_test_*.cpp` file (e.g. `lsp_test_navigation.cpp`, `lsp_test_completion.cpp`, `lsp_test_features.cpp`), using `lsp_test_helpers.hpp` and `lsp_test_fixtures.hpp`.
6. **Verify.** For a fast inner loop, build and run just the language-server tests — they all carry the CTest `lsp` label:

    ```bash
    cmake --build --preset default
    ctest --preset default -L lsp
    ```

    Then run the full suite once (`ctest --preset default`) to confirm nothing else broke. See [build-and-test.prompt.md](build-and-test.prompt.md) for the canonical build-and-test workflow.
