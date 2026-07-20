#include "analysis/source/source_manager.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"

namespace luma {

// ─── Helpers ───

namespace {

// Normalise a file path so that different textual representations of
// the same file map to the same cache key.  Falls back to the raw
// path if canonical resolution fails (e.g. the file does not yet
// exist on disk).
std::string normalize_path(std::string_view path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::path{path}, ec);

    if (ec) {
        return std::string{path};
    }

    return canonical.string();
}

// Compute the byte offset within `text` of the start of each line.  Line
// starts follow each '\n'; the first line always starts at offset 0, so the
// index has (number of '\n' + 1) entries — matching split_lines()' line count.
std::vector<std::uint32_t> compute_line_offsets(std::string_view text) {
    std::vector<std::uint32_t> offsets;
    offsets.push_back(0);

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            offsets.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }

    return offsets;
}

} // namespace

// ─── SourceFile constructor ───

SourceFile::SourceFile(FileId id, std::string file_path, std::string source_text)
    : file_id(id),
      path(std::move(file_path)),
      text(std::move(source_text)),
      line_offsets(compute_line_offsets(text)) {}

// ─── SourceManager ───

const SourceFile& SourceManager::load(std::string_view path) {
    const LoadResult result = try_load(path);

    if (result.file == nullptr) {
        throw std::runtime_error{result.error.value_or("failed to load source file")};
    }

    return *result.file;
}

LoadResult SourceManager::try_load(std::string_view path) {
    const auto normalized = normalize_path(path);
    const auto it = path_to_id_.find(normalized);

    if (it != path_to_id_.end()) {
        assert(it->second > 0 && "file_id must be positive (1-based indexing)");
        return LoadResult{.file = &files_.at(static_cast<std::size_t>(it->second - 1))};
    }

    // Binary mode so `text` holds exactly the on-disk bytes on every platform.
    // Without it, Windows text mode would translate CRLF→LF during the read
    // while the size check below sees raw bytes, and byte offsets into `text`
    // would not match the file.  The lexer already treats '\r' as whitespace
    // and get_line() strips a trailing '\r', so retaining CRLF is harmless.
    std::ifstream file_stream{normalized, std::ios::binary};

    if (!file_stream) {
        return LoadResult{.file = nullptr, .error = std::format("cannot open file '{}'", path)};
    }

    // Check file size before loading to prevent excessive memory use.
    file_stream.seekg(0, std::ios::end);
    const auto file_size = file_stream.tellg();
    file_stream.seekg(0, std::ios::beg);

    if (!file_stream || file_size < 0) {
        return LoadResult{.file = nullptr, .error = std::format("cannot read file '{}'", path)};
    }

    const auto max_source_size = static_cast<std::streamoff>(ResourceLimits::max_source_size);

    if (file_size > max_source_size) {
        return LoadResult{.file = nullptr,
                          .error = std::format("source file '{}' is too large ({} bytes, max {} "
                                               "bytes)",
                                               path, static_cast<std::size_t>(file_size),
                                               ResourceLimits::max_source_size)};
    }

    std::string text{std::istreambuf_iterator<char>(file_stream), std::istreambuf_iterator<char>()};

    strip_utf8_bom(text);

    const FileId file_id{next_file_id_++};

    path_to_id_[normalized] = file_id;

    files_.emplace_back(file_id, normalized, std::move(text));

    return LoadResult{.file = &files_.back()};
}

bool SourceManager::is_loaded(std::string_view path) const {
    return find_file_id(path).has_value();
}

std::string_view SourceManager::get_line(FileId file_id, int line) const {
    const SourceFile* source_file = get_file(file_id);

    if (source_file == nullptr) {
        return {};
    }

    const auto& offsets = source_file->line_offsets;

    if (line < 1 || line > static_cast<int>(offsets.size())) {
        return {};
    }

    const auto index = static_cast<std::size_t>(line - 1);
    const std::size_t start = offsets[index];
    // A line runs up to (but excluding) the '\n' that begins the next line,
    // or to end-of-text for the final line.
    const std::size_t end =
        (index + 1 < offsets.size()) ? offsets[index + 1] - 1 : source_file->text.size();

    std::string_view result{source_file->text};
    result = result.substr(start, end - start);

    // Strip a trailing '\r' so CRLF line endings render cleanly.
    if (!result.empty() && result.back() == '\r') {
        result.remove_suffix(1);
    }

    return result;
}

const SourceFile* SourceManager::get_file(FileId file_id) const {
    if (file_id < 1) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(file_id - 1);

    if (index >= files_.size()) {
        return nullptr;
    }

    return &files_[index];
}

std::optional<FileId> SourceManager::find_file_id(std::string_view path) const {
    const auto it = path_to_id_.find(normalize_path(path));

    if (it != path_to_id_.end()) {
        return it->second;
    }

    return std::nullopt;
}

const SourceFile& SourceManager::load_virtual(std::string_view name, std::string text) {
    // Virtual sources are keyed by their synthetic `name` verbatim — no
    // canonicalisation, no filesystem access.  A real file path could never
    // normalise to a bracketed sentinel such as "<gui-prelude>", so there is no
    // risk of colliding with a genuine on-disk file.
    const std::string key{name};
    const auto it = path_to_id_.find(key);

    if (it != path_to_id_.end()) {
        assert(it->second > 0 && "file_id must be positive (1-based indexing)");
        return files_.at(static_cast<std::size_t>(it->second - 1));
    }

    const FileId file_id{next_file_id_++};

    path_to_id_[key] = file_id;

    files_.emplace_back(file_id, key, std::move(text));

    return files_.back();
}

bool SourceManager::any_source_contains(std::string_view needle) const {
    return std::ranges::any_of(files_, [needle](const SourceFile& source_file) {
        return source_file.text.find(needle) != std::string::npos;
    });
}

bool SourceManager::any_source_contains_word(std::string_view word) const {
    return std::ranges::any_of(files_, [word](const SourceFile& source_file) {
        return contains_identifier_token(source_file.text, word);
    });
}

} // namespace luma
