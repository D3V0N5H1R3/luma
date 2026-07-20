#include "source_manager_locator.hpp"

#include "analysis/source/source_manager.hpp"

namespace luma::dap {

std::optional<FileId> SourceManagerLocator::find_file_id(std::string_view path) const {
    return source_manager_->find_file_id(path);
}

const SourceFile* SourceManagerLocator::get_file(FileId file_id) const {
    return source_manager_->get_file(file_id);
}

void SourceManagerLocator::for_each_file(
    std::function<void(FileId file_id, const SourceFile*)> fn) const {
    for (FileId fid = 1;; ++fid) {
        const auto* file = source_manager_->get_file(fid);

        if (file == nullptr) {
            break;
        }

        fn(fid, file);
    }
}

} // namespace luma::dap
