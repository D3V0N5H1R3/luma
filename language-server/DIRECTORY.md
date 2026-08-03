# Luma Language Server (`luma_lsp`)

A minimal [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) implementation for the [Luma](https://github.com/d3v0n5h1r3/luma) programming language. The server is a standalone C++ executable that communicates over standard input/output using JSON-RPC 2.0.

## Features

| Capability              | Description                                                      |
| ----------------------- | ---------------------------------------------------------------- |
| Diagnostics             | Real-time syntax errors and type errors as you type              |
| Hover                   | Type information, function signatures, and module documentation  |
| Completions             | Standard library module members and user-defined symbols         |
| Completion resolve      | Rich detail on selected completion items                         |
| Signature help          | Parameter hints when typing function arguments                   |
| Go to definition        | Jump to variable, function, record, and choice type declarations |
| Go to type definition   | Jump to the type of a variable or expression                     |
| Go to implementation    | Jump to interface implementations                                |
| Find references         | Locate all usages of a symbol in the current file                |
| Rename                  | Rename a symbol and all its references (with prepare support)    |
| Document symbols        | Outline view of functions, records, and choice types             |
| Workspace symbols       | Search symbols across all open documents                         |
| Semantic tokens         | Full semantic highlighting (annotations, types, modules, etc.)   |
| Semantic tokens (range) | Range-scoped semantic highlighting                               |
| Code actions            | Quick fixes for common errors (mutable, unused vars, etc.)       |
| Code lens               | Reference counts on functions and types                          |
| Folding ranges          | Code folding for blocks, declarations, and comments              |
| Inlay hints             | Inferred type annotations for variables                          |
| Document highlight      | Highlight all occurrences of a symbol in the current document    |
| Selection range         | Smart expand/shrink selection                                    |
| Call hierarchy          | Incoming and outgoing call graphs                                |
| Type hierarchy          | Supertypes and subtypes for interfaces and records               |
| Linked editing ranges   | Simultaneous editing of related identifiers                      |
| Document links          | Clickable include paths                                          |
| Document formatting     | Format entire document                                           |
| Range formatting        | Format a selected range                                          |
| Execute command         | Server-side command execution (e.g. show references)             |

## Supported Editors

The server works with any editor that supports LSP over stdio:

- **Visual Studio Code** — via the [Luma extension](../extensions/vscode/)
- **Zed** — via the [Luma extension](../extensions/zed/)
- **Helix** — add to `languages.toml`

## Building

The language server is built as part of the main CMake project. Using the CMake presets:

```bash
cmake --preset default
cmake --build --preset default --target luma_lsp
```

Or with a classic configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target luma_lsp --parallel
```

This produces `build/luma_lsp` on Linux and macOS, or `build\Release\luma_lsp.exe` on Windows with MSVC.

## Usage

The server reads JSON-RPC messages from stdin and writes responses to stdout. Editors spawn it automatically, but you can send a single framed request manually with `printf` (which, unlike `echo`, emits the carriage returns the protocol requires):

```bash
printf 'Content-Length: 58\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./build/luma_lsp
```

### Editor Configuration

**Helix (languages.toml):**

```toml
[[language]]
name = "luma"
scope = "source.luma"
file-types = ["luma"]
language-servers = ["luma-lsp"]

[language-server.luma-lsp]
command = "luma_lsp"
```

## Architecture

```text
┌─────────────────────────────────────────────────┐
│     Editor — VS Code / Zed / Helix     │
│                   LSP client                    │
└─────────────────────────────────────────────────┘
    JSON-RPC over stdio (Content-Length framed)
┌─────────────────────────────────────────────────┐
│                    luma_lsp                     │
│                                                 │
│   Main thread          Analysis worker thread   │
│   JSON-RPC dispatch    lexer / parser / types   │
└─────────────────────────────────────────────────┘
```

The server uses a dedicated analysis worker thread. On each document change it re-runs the Luma lexer, parser, and type checker to produce fresh diagnostics and symbol information. The main thread handles JSON-RPC dispatch while the worker performs analysis asynchronously.

### Source Layout

```text
source/
├── main.cpp                              # Entry point (stdio binary mode setup)
│
│   ── Analysis Pipeline ──
├── lsp_analysis_cache.hpp/cpp            # LRU analysis result caching
├── lsp_analysis_pipeline.hpp/cpp         # Background analysis worker thread
├── lsp_analysis_result.hpp               # Analysis result types
├── lsp_analysis_service.hpp              # AnalysisService interface
├── lsp_analysis_service_impl.hpp/cpp     # AnalysisService implementation (phases 1–6)
├── lsp_analysis_service_symbols.cpp      # Symbol collection and interface matching
├── lsp_analysis_view.hpp                 # Read-only view over analysis results
│
│   ── Server Core ──
├── lsp_server.hpp/cpp                    # Server construction and startup
├── lsp_server_dispatch.cpp               # JSON-RPC method dispatch
├── lsp_server_lifecycle.cpp              # Initialize/shutdown handlers
├── lsp_server_state_lock.hpp             # Typed state-lock wrappers
├── lsp_handler_context.hpp               # Shared context passed to handlers
├── lsp_handler_registry.hpp              # Handler registration (header-only)
├── lsp_cancellation_manager.hpp          # Request cancellation tracking
├── lsp_transport.hpp                     # Content-Length framed JSON-RPC transport
├── lsp_transport_wrapper.hpp/cpp         # Thread-safe transport with convenience methods
│
│   ── Document Management ──
├── lsp_document_store.hpp/cpp            # Open document text storage
├── lsp_document_synchronizer.hpp/cpp     # didOpen/didChange/didClose handlers
├── lsp_server_sync.cpp                   # Text document sync dispatch
│
│   ── Feature Handlers ──
├── lsp_server_code_actions.cpp           # Code actions, quick fixes, and code lens
├── lsp_server_code_actions_refactoring.cpp # Refactoring code actions
├── lsp_server_completion.cpp             # Completion trigger and filtering
├── lsp_server_completion_resolve.cpp     # Completion item resolve
├── lsp_server_folding.cpp                # Folding ranges for blocks, declarations, and comments
├── lsp_server_formatting.cpp             # Document and range formatting
├── lsp_server_hierarchy.cpp              # Call and type hierarchy requests
├── lsp_server_hover.cpp                  # Hover requests
├── lsp_server_inlay.cpp                  # Inlay hints for inferred types and parameter names
├── lsp_server_navigation.cpp             # Definition, references, document links, and selection range
├── lsp_server_rename.cpp                 # Rename, prepare-rename, and linked editing
├── lsp_server_semantic_tokens.cpp        # Semantic token encoding and delta
├── lsp_server_signature.cpp              # Signature help
├── lsp_server_symbols.cpp                # Document and workspace symbols
├── lsp_server_workspace.cpp              # Workspace folder and file watching
│
│   ── Handler Interfaces ──
├── lsp_code_action_handler.hpp           # Code action handler interface
├── lsp_completion_handler.hpp            # Completion handler interface
├── lsp_folding_handler.hpp               # Folding range handler interface
├── lsp_formatting_handler.hpp            # Formatting handler interface
├── lsp_hierarchy_handler.hpp             # Hierarchy handler interface
├── lsp_hover_handler.hpp                 # Hover handler interface
├── lsp_inlay_hint_handler.hpp            # Inlay hint handler interface
├── lsp_navigation_handler.hpp            # Navigation handler interface
├── lsp_quickfix_handler.hpp              # Quick fix handler interface
├── lsp_refactoring_provider.hpp          # Refactoring provider framework
├── lsp_rename_handler.hpp                # Rename handler interface
├── lsp_semantic_tokens_handler.hpp       # Semantic tokens handler interface
├── lsp_symbol_handler.hpp                # Symbol handler interface
├── lsp_sync_handler.hpp                  # Sync handler interface
├── lsp_workspace_handler.hpp             # Workspace handler interface
│
│   ── Completion Support ──
├── lsp_completion_provider.hpp/cpp       # Completion strategy dispatch
├── lsp_completion_strategy.hpp/cpp       # Completion strategy implementations
├── lsp_keyword_catalog.hpp/cpp           # Keyword completion catalog
├── lsp_stdlib_registry.hpp/cpp           # Stdlib function registry for completions
│
│   ── Symbol Resolution ──
├── lsp_definition_resolver.hpp           # Definition location resolution
├── lsp_identifier_collector.hpp/cpp      # Scoped identifier occurrence collection
├── lsp_include_processor.hpp/cpp         # Include resolution for multi-file analysis
├── lsp_scope_stack.hpp/cpp               # Scope-based symbol visibility
├── lsp_symbol_lookup.hpp                 # Symbol lookup helpers
├── lsp_symbol_resolver.hpp/cpp           # Symbol resolution for go-to-definition
│
│   ── Workspace Indexing ──
├── lsp_persisted_index.hpp/cpp           # Workspace index persistence (binary format)
├── lsp_workspace_indexer.hpp/cpp         # Workspace-wide background indexing
├── lsp_workspace_manager.hpp/cpp         # Multi-root workspace management
├── lsp_pending_uri_set.hpp               # Thread-safe pending analysis URI set
│
│   ── Utilities ──
├── lsp_binary_format.hpp                 # Big-endian read/write helpers
├── lsp_brace_matcher.hpp                 # Bracket/brace matching
├── lsp_capabilities.hpp/cpp              # Server capability registration
├── lsp_code_action_builder.hpp           # Code action JSON builder
├── lsp_config.hpp                        # Server configuration constants
├── lsp_configuration_manager.hpp/cpp     # Runtime configuration handling
├── lsp_constants.hpp                     # Centralised LSP constants
├── lsp_diagnostic_builder.hpp/cpp        # Diagnostic construction
├── lsp_diagnostic_codes.hpp              # Diagnostic code constants
├── lsp_exception_utils.hpp               # Exception formatting utility
├── lsp_hover_literals.hpp/cpp            # Hover content for literal expressions
├── lsp_lexical_context.hpp               # String/comment/interpolation context for raw text
├── lsp_lock_utils.hpp                    # Lock utilities (adapters + ordered guard)
├── lsp_optional_ref.hpp                  # Non-owning optional reference
├── lsp_param_extraction.hpp              # JSON parameter extraction helpers
├── lsp_param_utils.hpp                   # Parameter string parsing utilities
├── lsp_params.hpp                        # LSP request parameter types
├── lsp_path_utils.hpp                    # Path normalisation utilities
├── lsp_position_utils.hpp                # UTF-16 offset and position conversion
├── lsp_response_helpers.hpp              # Response JSON construction helpers
├── lsp_semantic_token_cache.hpp          # Per-document semantic token caching
├── lsp_string_utils.hpp                  # String manipulation helpers
├── lsp_text_formatter.hpp/cpp            # Line/token-based source formatting engine
├── lsp_token_classifier.hpp              # Token and symbol classification
├── lsp_token_index.hpp                   # Per-line token index
├── lsp_token_utils.hpp/cpp               # Token position and range helpers
├── lsp_type_formatter.hpp                # Type annotation rendering
└── lsp_types.hpp/cpp                     # LSP protocol type definitions
```

The source tree is split by request family so new handlers can be added without growing a single catch-all implementation file.

### Design Principles

- **Reuses the interpreter pipeline** — the same lexer, parser, and type checker used by the `luma` executable.
- **Minimal third-party dependencies** — C++20 standard library primarily; external libraries only as exceptions (see `instructions/cpp.instructions.md` §7).
- **No runtime execution** — analysis only; never runs user code.
- **Full re-analysis on each edit** — simple and correct; fast enough for interactive use.

## Protocol Details

The server advertises these capabilities during `initialize`:

- `textDocumentSync`: incremental document sync (open/change/close)
- `hoverProvider`: true
- `completionProvider`: trigger characters `.`, `>` (for `|>`), with resolve support
- `signatureHelpProvider`: trigger characters `(`, `,`
- `definitionProvider`: true
- `typeDefinitionProvider`: true
- `implementationProvider`: true
- `referencesProvider`: true
- `renameProvider`: true (with prepare support)
- `documentSymbolProvider`: true
- `workspaceSymbolProvider`: true
- `codeActionProvider`: true
- `codeLensProvider`: true
- `foldingRangeProvider`: true
- `inlayHintProvider`: true
- `documentHighlightProvider`: true
- `selectionRangeProvider`: true
- `callHierarchyProvider`: true
- `typeHierarchyProvider`: true
- `linkedEditingRangeProvider`: true
- `documentLinkProvider`: true
- `documentFormattingProvider`: true
- `documentRangeFormattingProvider`: true
- `executeCommandProvider`: `luma.showReferences`
- `semanticTokensProvider`: full and range tokens with legend
- `positionEncoding`: UTF-16

## Further Reading

- [Luma Language Server Design Document](../documents/Luma_Language_Server.md) — detailed architecture and data flow
- [Luma Software Architecture](../documents/Luma_Software_Architecture.md) — interpreter pipeline that the LSP reuses
