#ifndef LUMA_DAP_SOURCE_MANAGER_LOCATOR_HPP
#define LUMA_DAP_SOURCE_MANAGER_LOCATOR_HPP

#include "i_source_locator.hpp"

namespace luma {
class SourceManager;
} // namespace luma

namespace luma::dap {

// Adapter that wraps a concrete SourceManager to satisfy the
// ISourceLocator interface, keeping the debugger decoupled from the
// core SourceManager type.
class SourceManagerLocator final : public ISourceLocator {
public:
    explicit SourceManagerLocator(SourceManager* sm) : source_manager_(sm) {}

    [[nodiscard]] std::optional<FileId> find_file_id(std::string_view path) const override;
    [[nodiscard]] const SourceFile* get_file(FileId file_id) const override;
    void for_each_file(std::function<void(FileId file_id, const SourceFile*)> fn) const override;

private:
    SourceManager* source_manager_;
};

} // namespace luma::dap

#endif // LUMA_DAP_SOURCE_MANAGER_LOCATOR_HPP
