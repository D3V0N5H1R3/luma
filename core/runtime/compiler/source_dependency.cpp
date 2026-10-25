#include "runtime/compiler/source_dependency.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <system_error>

#include "analysis/source/source_manager.hpp"
#include "common/hash.hpp"
#include "common/resource_limits.hpp"
#include "common/string_utils.hpp"

namespace luma {

namespace {

// Read `path` the same way SourceManager::try_load does — binary, size-capped,
// UTF-8 BOM stripped — so the hash matches the one recorded at compile time.
//
// The paths may come from an untrusted .lumc file, so only regular files are
// read (a FIFO would block and a device such as /dev/zero never ends), and
// exactly the size that passed the limit check is read rather than the whole
// stream.
[[nodiscard]] std::optional<std::string> read_source_text(const std::string& path) {
    std::error_code ec;

    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }

    std::ifstream stream{path, std::ios::binary | std::ios::ate};

    if (!stream) {
        return std::nullopt;
    }

    const auto size = stream.tellg();

    if (size < 0 || static_cast<std::size_t>(size) > ResourceLimits::max_source_size) {
        return std::nullopt;
    }

    stream.seekg(0, std::ios::beg);

    const auto byte_count = static_cast<std::streamsize>(size);
    std::string text(static_cast<std::size_t>(byte_count), '\0');
    stream.read(text.data(), byte_count);

    if (stream.gcount() != byte_count) {
        return std::nullopt;
    }

    strip_utf8_bom(text);
    return text;
}

} // namespace

std::vector<SourceDependency> collect_source_dependencies(const SourceManager& source_manager,
                                                          FileId root) {
    std::vector<SourceDependency> dependencies;

    for (const SourceFile& file : source_manager.files()) {
        if (file.is_virtual || file.file_id == root) {
            continue;
        }

        dependencies.push_back(
            SourceDependency{.path = file.path, .content_hash = fnv1a_hash(file.text)});
    }

    return dependencies;
}

bool are_source_dependencies_current(std::span<const SourceDependency> dependencies) {
    return std::ranges::all_of(dependencies, [](const SourceDependency& dependency) {
        const auto text = read_source_text(dependency.path);

        return text.has_value() && fnv1a_hash(*text) == dependency.content_hash;
    });
}

} // namespace luma
