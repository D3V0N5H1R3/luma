#ifndef LUMA_DAP_I_SOURCE_LOCATOR_HPP
#define LUMA_DAP_I_SOURCE_LOCATOR_HPP

#include <functional>
#include <optional>
#include <string_view>

#include "analysis/source/source_location.hpp"

namespace luma {
struct SourceFile;
} // namespace luma

namespace luma::dap {

// Abstraction over source-file lookup, used by BreakpointManager to
// resolve paths to file IDs and retrieve SourceFile metadata without
// depending on the concrete SourceManager type.
class ISourceLocator {
public:
    virtual ~ISourceLocator() = default;

    // Find the file_id for a given absolute path.
    // Returns std::nullopt if the path has not been loaded.
    [[nodiscard]] virtual std::optional<FileId> find_file_id(std::string_view path) const = 0;

    // Return the SourceFile for a given file_id, or nullptr if not found.
    [[nodiscard]] virtual const SourceFile* get_file(FileId file_id) const = 0;

    // Iterate all loaded files, calling fn(file_id, SourceFileInfo) for each.
    virtual void for_each_file(std::function<void(FileId file_id, const SourceFile*)> fn) const = 0;
};

} // namespace luma::dap

#endif // LUMA_DAP_I_SOURCE_LOCATOR_HPP
