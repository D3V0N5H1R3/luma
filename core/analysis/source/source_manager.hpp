#ifndef LUMA_SOURCE_SOURCE_MANAGER_HPP
#define LUMA_SOURCE_SOURCE_MANAGER_HPP

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Holds the text of a single loaded source file plus a line-offset index.
// Move-only: copying a SourceFile (which owns the full text) is expensive and
// almost certainly a bug.
struct SourceFile {
    FileId file_id{0};
    std::string path;
    std::string text;
    // Byte offset within `text` of the start of each line (1 entry per line).
    // A compact index (~4 bytes/line) instead of a second full copy of the
    // text; get_line() slices `text` using these offsets.  uint32_t is
    // sufficient because ResourceLimits::max_source_size caps files well below
    // 4 GiB.
    std::vector<std::uint32_t> line_offsets;

    // Construct a source file from its components.
    // The line-offset index is built from source_text automatically.
    SourceFile(FileId id, std::string file_path, std::string source_text);

    SourceFile(const SourceFile&) = delete;
    SourceFile& operator=(const SourceFile&) = delete;
    SourceFile(SourceFile&&) = default;
    SourceFile& operator=(SourceFile&&) = default;
    ~SourceFile() = default;
};

// Returned by SourceManager::try_load(). Bundles the loaded file pointer and
// any failure message into a single value so callers never need an output
// parameter.
//
// The raw `const SourceFile*` member is intentional: LoadResult acts as a
// discriminated success/failure pair where `file` is non-null on success and
// nullptr on failure, mirroring the `operator bool()` conversion.  This is
// cleaner than `std::optional<std::reference_wrapper<const SourceFile>>`
// because callers already check the result with `if (result)` and then
// access `result.file->` directly — the pointer indirection is idiomatic
// and consistent with the rest of the API (e.g. SourceManager::get_file()).
//
// `error` is a plain message string, not a structured Diagnostic: source
// loading has no meaningful source span of its own, and every caller either
// re-wraps the text with the include site's real location or throws it.  This
// also keeps the source module free of any dependency on the diagnostics module.
struct LoadResult {
    const SourceFile* file{nullptr};  // Non-null on success.
    std::optional<std::string> error; // Populated on failure.

    [[nodiscard]] explicit operator bool() const noexcept {
        return file != nullptr;
    }
};

// Loads source files from disk, assigns each a unique file_id, and provides
// efficient per-line access for error reporting.
class SourceManager {
public:
    // Load a source file by path.  Returns the SourceFile on success.
    //
    // Use when the file *must* exist — failure is truly exceptional (e.g.
    // the entry-point file supplied on the command line).  Throws
    // std::runtime_error if the file cannot be loaded (it cannot be opened or
    // read, or it exceeds ResourceLimits::max_source_size).
    //
    // Each file is only loaded once; subsequent calls with the same path
    // return the previously loaded entry (idempotent).
    [[nodiscard]] const SourceFile& load(std::string_view path);

    // Non-throwing alternative to load().
    //
    // Use when failure is an expected outcome that the caller can handle
    // gracefully — for example, include resolution where a candidate path
    // may not exist.  Returns a LoadResult whose `file` member is non-null
    // on success.  On failure, `file` is nullptr and `error` holds a plain
    // message describing the problem (see LoadResult for why it is a message
    // string, not a structured Diagnostic).
    [[nodiscard("Returned LoadResult.file is nullptr on failure")]]
    LoadResult try_load(std::string_view path);

    // Check whether a file has already been loaded.
    //
    // Note: like find_file_id() and try_load(), this normalises `path` with
    // std::filesystem::weakly_canonical, which touches the filesystem.  Hot
    // callers that already hold a canonical path should avoid calling it in a
    // tight loop.
    [[nodiscard]] bool is_loaded(std::string_view path) const;

    // Retrieve a specific line (1-based) from a loaded file.
    // Returns empty string_view if file_id or line number is out of range.
    [[nodiscard]] std::string_view get_line(FileId file_id, int line) const;

    // Return the SourceFile for a given file_id, or nullptr if not found.
    [[nodiscard]] const SourceFile* get_file(FileId file_id) const;

    // Find the file_id for a given absolute path.
    // Returns std::nullopt if the path has not been loaded.
    //
    // Note: normalises `path` with std::filesystem::weakly_canonical, which
    // touches the filesystem — see the note on is_loaded().
    [[nodiscard]] std::optional<FileId> find_file_id(std::string_view path) const;

    // Register in-memory text as a virtual source file under a synthetic `name`
    // (e.g. "<gui-prelude>"), assigning it a fresh file_id without touching the
    // filesystem or canonicalising the name.  Used for compiler-injected code
    // such as the built-in Solaris prelude so that its source locations
    // attribute to a stable virtual file and its diagnostics still render with
    // real source context.  Idempotent by `name`: a repeated call returns the
    // previously registered entry.
    const SourceFile& load_virtual(std::string_view name, std::string text);

    // Return true when any loaded source file's text contains `needle`.  Used
    // as a cheap gate for conditional prelude injection across a multi-file
    // program (root plus resolved includes).
    [[nodiscard]] bool any_source_contains(std::string_view needle) const;

    // Return true when any loaded source file's text contains `word` as a
    // standalone identifier (not embedded in a longer name).  A whole-word
    // variant of any_source_contains() for name-triggered gates such as
    // prelude injection, where matching a substring of an unrelated identifier
    // would be a false positive.
    [[nodiscard]] bool any_source_contains_word(std::string_view word) const;

private:
    FileId next_file_id_{1}; ///< Next file_id to assign (0 is reserved for "<input>" / "<repl>").
    std::deque<SourceFile> files_; ///< Loaded source files, indexed by (file_id - 1).
    StringMap<FileId> path_to_id_; ///< Normalised path → file_id lookup cache.
};

} // namespace luma

#endif // LUMA_SOURCE_SOURCE_MANAGER_HPP
