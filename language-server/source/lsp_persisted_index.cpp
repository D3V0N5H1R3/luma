#include "lsp_persisted_index.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lsp_binary_format.hpp"

namespace luma::lsp {

namespace {

// Continue an FNV-1a hash with a single byte.
void fnv1a_mix_byte(std::uint32_t& hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= 0x01000193u;
}

// Mix a length-delimited string so that different splits cannot collide.
void fnv1a_mix_bytes(std::uint32_t& hash, std::string_view data) {
    for (const char c : data) {
        fnv1a_mix_byte(hash, static_cast<std::uint8_t>(c));
    }
    fnv1a_mix_byte(hash, std::uint8_t{0});
}

void fnv1a_mix_u64(std::uint32_t& hash, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        fnv1a_mix_byte(hash, static_cast<std::uint8_t>(value & 0xFFu));
        value >>= 8;
    }
}

void fnv1a_mix_vec(std::uint32_t& hash, const std::vector<std::string>& items) {
    fnv1a_mix_u64(hash, items.size());
    for (const auto& s : items) {
        fnv1a_mix_bytes(hash, s);
    }
}

// Order-independent content digest of every entry. Each entry is folded into an
// independent FNV-1a hash and the per-entry hashes are XORed together, so the
// unordered_map iteration order (which differs between save and load) does not
// affect the result. Folding this into the integrity checksum means on-disk
// corruption of any entry field — not just a mismatched version/count — is
// detected when the index is next loaded.
[[nodiscard]] std::uint32_t
content_digest(const std::unordered_map<std::string, IndexedFileEntry>& entries) {
    std::uint32_t digest = 0;

    for (const auto& [path, entry] : entries) {
        std::uint32_t h = 0x811c9dc5u;
        fnv1a_mix_bytes(h, path);
        fnv1a_mix_u64(h, static_cast<std::uint64_t>(entry.content_hash));
        fnv1a_mix_u64(h, entry.last_modified);
        fnv1a_mix_vec(h, entry.function_names);
        fnv1a_mix_vec(h, entry.record_names);
        fnv1a_mix_vec(h, entry.choice_names);
        fnv1a_mix_vec(h, entry.exported_symbols);
        fnv1a_mix_byte(h, static_cast<std::uint8_t>((entry.has_main ? 0x01u : 0u) |
                                                    (entry.has_tests ? 0x02u : 0u)));
        digest ^= h;
    }

    return digest;
}

} // namespace

// ─── PersistedIndex Implementation ───

bool PersistedIndex::load(const std::filesystem::path& index_path) {
    std::ifstream file(index_path, std::ios::binary);
    if (!file) {
        return false;
    }

    // Read and validate magic.
    char magic[4]{};
    file.read(magic, 4);
    if (std::memcmp(magic, k_magic, 4) != 0) {
        return false;
    }

    // Read and validate version.
    auto version = binary_format::read_u32(file);
    if (version != k_version) {
        return false;
    }

    // Read file count.
    auto file_count = binary_format::read_u32(file);
    if (file_count > binary_format::k_max_collection_count) {
        return false; // Sanity limit.
    }

    entries_.clear();
    entries_.reserve(file_count);

    for (std::uint32_t i = 0; i < file_count; ++i) {
        IndexedFileEntry entry;
        entry.path = binary_format::read_string(file);
        entry.content_hash = static_cast<std::size_t>(binary_format::read_u64(file));
        entry.last_modified = binary_format::read_u64(file);
        entry.function_names = binary_format::read_string_vec(file);
        entry.record_names = binary_format::read_string_vec(file);
        entry.choice_names = binary_format::read_string_vec(file);
        entry.exported_symbols = binary_format::read_string_vec(file);

        auto flags = static_cast<std::uint8_t>(file.get());
        entry.has_main = (flags & 0x01) != 0;
        entry.has_tests = (flags & 0x02) != 0;

        if (!file.good()) {
            entries_.clear();
            return false;
        }

        entries_[entry.path] = std::move(entry);
    }

    // Verify integrity checksum. It covers the header fields (version, count)
    // XORed with a content digest of every entry, so corruption anywhere in the
    // entry region — not just the header — is detected.
    std::uint32_t raw = k_version ^ file_count;
    auto expected =
        fnv1a_hash(reinterpret_cast<const char*>(&raw), sizeof(raw)) ^ content_digest(entries_);
    auto stored = binary_format::read_u32(file);
    if (!file.good() || stored != expected) {
        entries_.clear();
        return false;
    }

    return true;
}

bool PersistedIndex::save(const std::filesystem::path& index_path) const {
    // Ensure parent directory exists.
    auto parent = index_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(index_path, std::ios::binary);
    if (!file) {
        return false;
    }

    const auto entry_count = static_cast<std::uint32_t>(entries_.size());

    // Header.
    file.write(k_magic, 4);
    binary_format::write_u32(file, k_version);
    binary_format::write_u32(file, entry_count);

    // Entries.
    for (const auto& [_, entry] : entries_) {
        binary_format::write_string(file, entry.path);
        binary_format::write_u64(file, static_cast<std::uint64_t>(entry.content_hash));
        binary_format::write_u64(file, entry.last_modified);
        binary_format::write_string_vec(file, entry.function_names);
        binary_format::write_string_vec(file, entry.record_names);
        binary_format::write_string_vec(file, entry.choice_names);
        binary_format::write_string_vec(file, entry.exported_symbols);

        std::uint8_t flags = 0;
        if (entry.has_main) {
            flags |= 0x01;
        }
        if (entry.has_tests) {
            flags |= 0x02;
        }
        file.put(static_cast<char>(flags));
    }

    // Integrity checksum: FNV-1a of the header fields (version, entry count)
    // XORed with a content digest of every entry, so on-disk corruption of the
    // entry region is detected on load — not just a mismatched version/count.
    std::uint32_t raw = k_version ^ entry_count;
    auto checksum =
        fnv1a_hash(reinterpret_cast<const char*>(&raw), sizeof(raw)) ^ content_digest(entries_);
    binary_format::write_u32(file, checksum);

    return file.good();
}

std::size_t PersistedIndex::validate() {
    return std::erase_if(entries_, [](const auto& kv) {
        const auto& entry = kv.second;
        const std::filesystem::path file_path(entry.path);

        std::error_code ec;
        if (!std::filesystem::exists(file_path, ec)) {
            return true;
        }

        // If a last_modified timestamp was stored, verify it still matches.
        if (entry.last_modified != 0) {
            auto ftime = std::filesystem::last_write_time(file_path, ec);
            if (!ec) {
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                auto epoch = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch())
                        .count());
                if (epoch != entry.last_modified) {
                    return true;
                }
            }
        }

        return false;
    });
}

optional_ref<const IndexedFileEntry> PersistedIndex::find(const std::string& path) const {
    auto it = entries_.find(path);
    if (it != entries_.end()) {
        return it->second;
    }
    return {};
}

bool PersistedIndex::is_valid(const std::string& path, std::size_t current_hash) const {
    auto entry = find(path);
    return entry.has_value() && entry->content_hash == current_hash;
}

void PersistedIndex::upsert(IndexedFileEntry entry) {
    entries_[entry.path] = std::move(entry);
}

void PersistedIndex::remove(const std::string& path) {
    entries_.erase(path);
}

std::vector<std::string> PersistedIndex::all_paths() const {
    std::vector<std::string> paths;
    paths.reserve(entries_.size());
    for (const auto& [path, _] : entries_) {
        paths.push_back(path);
    }
    return paths;
}

void PersistedIndex::clear() {
    entries_.clear();
}

std::filesystem::path PersistedIndex::default_path(const std::filesystem::path& workspace_root) {
    return workspace_root / ".luma" / "index.lidx";
}

} // namespace luma::lsp
