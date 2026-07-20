#include "lsp_analysis_cache.hpp"

#include <stdexcept>
#include <utility>

namespace luma::lsp {

LspAnalysisCache::LspAnalysisCache(std::size_t max_entries) : max_entries_(max_entries) {}

optional_ref<const AnalysisResult> LspAnalysisCache::find(const std::string& uri) const {
    auto it = cache_.find(uri);
    if (it != cache_.end()) {
        return it->second;
    }
    return {};
}

optional_ref<AnalysisResult> LspAnalysisCache::find(const std::string& uri) {
    auto it = cache_.find(uri);
    if (it != cache_.end()) {
        return it->second;
    }
    return {};
}

void LspAnalysisCache::insert(const std::string& uri, AnalysisResult result) {
    cache_[uri] = std::move(result);
    touch(uri);
    // Refresh the reverse index for this file: drop the previous version's
    // symbols (if any), then index the newly-stored result.
    unindex_uri(uri);
    index_uri(uri);
}

AnalysisResult& LspAnalysisCache::at(const std::string& uri) {
    // Precondition: uri must already exist (caller must have called insert() first).
    // Use cache_.at() so that a missing key throws std::out_of_range rather than
    // silently inserting a default entry that bypasses LRU tracking.
    return cache_.at(uri);
}

void LspAnalysisCache::remove(const std::string& uri) {
    cache_.erase(uri);
    if (auto it = lru_index_.find(uri); it != lru_index_.end()) {
        lru_list_.erase(it->second);
        lru_index_.erase(it);
    }
    unindex_uri(uri);
}

bool LspAnalysisCache::contains(const std::string& uri) const {
    return cache_.contains(uri);
}

std::size_t LspAnalysisCache::size() const {
    return cache_.size();
}

void LspAnalysisCache::touch(const std::string& uri) {
    if (auto it = lru_index_.find(uri); it != lru_index_.end()) {
        lru_list_.erase(it->second);
        lru_index_.erase(it);
    }
    lru_list_.push_back(uri);
    lru_index_[uri] = std::prev(lru_list_.end());
}

bool LspAnalysisCache::evict_one(const std::function<bool(const std::string&)>& can_evict) {
    for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it) {
        if (can_evict(*it)) {
            const std::string evicted = *it;
            cache_.erase(evicted);
            lru_index_.erase(evicted);
            lru_list_.erase(it);
            unindex_uri(evicted);
            return true;
        }
    }
    return false;
}

void LspAnalysisCache::evict_to_limit(const std::function<bool(const std::string&)>& can_evict) {
    // Track how many consecutive pinned entries we have skipped. When skipped reaches
    // lru_list_.size() we have completed a full rotation without finding anything
    // evictable, so all remaining entries are pinned and we must give up.
    std::size_t skipped = 0;
    while (lru_list_.size() > max_entries_) {
        if (skipped >= lru_list_.size()) {
            break; // All remaining entries are pinned; cannot reduce further.
        }
        const auto uri_copy = lru_list_.front();
        if (can_evict(uri_copy)) {
            skipped = 0;
            cache_.erase(uri_copy);
            lru_index_.erase(uri_copy);
            lru_list_.pop_front();
            unindex_uri(uri_copy);
        } else {
            ++skipped;
            lru_list_.splice(lru_list_.end(), lru_list_, lru_list_.begin());
            lru_index_[lru_list_.back()] = std::prev(lru_list_.end());
        }
    }
}

void LspAnalysisCache::add_include_dependent(const std::string& include_uri,
                                             const std::string& dependent_uri) {
    include_dependents_[include_uri].insert(dependent_uri);
}

optional_ref<const std::unordered_set<std::string>>
LspAnalysisCache::get_dependents(const std::string& include_uri) const {
    auto it = include_dependents_.find(include_uri);
    if (it != include_dependents_.end()) {
        return it->second;
    }
    return {};
}

void LspAnalysisCache::remove_dependent(const std::string& uri) {
    for (auto& [_, deps] : include_dependents_) {
        deps.erase(uri);
    }
}

CacheTransaction LspAnalysisCache::begin_update(const std::string& uri) {
    return CacheTransaction(*this, uri);
}

// ═══════════════════════════════════════════════════════════════════════════
// Cross-file symbol reverse index
// ═══════════════════════════════════════════════════════════════════════════

void LspAnalysisCache::index_uri(const std::string& uri) {
    auto cache_it = cache_.find(uri);
    if (cache_it == cache_.end()) {
        return;
    }

    const auto& symbols = cache_it->second.semantic.symbols;
    auto& names = uri_symbols_[uri];
    names.reserve(symbols.definitions.size() + symbols.user_functions.size());

    for (const auto& [name, def] : symbols.definitions) {
        symbol_index_[name].push_back(IndexEntry{.uri = uri, .location = def.location});
        names.push_back(name);
    }
    for (const auto& [name, info] : symbols.user_functions) {
        symbol_index_[name].push_back(IndexEntry{.uri = uri, .location = info.location});
        names.push_back(name);
    }
}

void LspAnalysisCache::unindex_uri(const std::string& uri) {
    auto names_it = uri_symbols_.find(uri);
    if (names_it == uri_symbols_.end()) {
        return;
    }

    for (const auto& name : names_it->second) {
        auto bucket_it = symbol_index_.find(name);
        if (bucket_it == symbol_index_.end()) {
            continue;
        }
        auto& bucket = bucket_it->second;
        std::erase_if(bucket, [&uri](const IndexEntry& entry) { return entry.uri == uri; });
        if (bucket.empty()) {
            symbol_index_.erase(bucket_it);
        }
    }

    uri_symbols_.erase(names_it);
}

std::optional<LspAnalysisCache::SymbolLocation>
LspAnalysisCache::lookup_symbol(const std::string& name, const std::string& exclude_uri) const {
    auto it = symbol_index_.find(name);
    if (it == symbol_index_.end()) {
        return std::nullopt;
    }
    for (const auto& entry : it->second) {
        if (entry.uri != exclude_uri) {
            return SymbolLocation{.uri = entry.uri, .location = entry.location};
        }
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════════════
// CacheTransaction implementation
// ═══════════════════════════════════════════════════════════════════════════

CacheTransaction::CacheTransaction(LspAnalysisCache& cache, std::string uri)
    : cache_(&cache), uri_(std::move(uri)) {}

CacheTransaction::~CacheTransaction() {
    // Implicit rollback: if commit() was never called, staged changes are
    // simply dropped — the cache remains untouched.
}

// Members are moved, not copied; the only escape path the analyzer can trace is
// MSVC STL bad_alloc, which a move never triggers.
// NOLINTNEXTLINE(bugprone-exception-escape)
CacheTransaction::CacheTransaction(CacheTransaction&& other) noexcept
    : cache_(other.cache_),
      uri_(std::move(other.uri_)),
      pending_result_(std::move(other.pending_result_)),
      pending_include_deps_(std::move(other.pending_include_deps_)),
      finished_(other.finished_) {
    other.finished_ = true; // prevent the moved-from instance from committing
}

void CacheTransaction::set_result(AnalysisResult result) {
    if (finished_) {
        throw std::logic_error("CacheTransaction::set_result called on a finished transaction");
    }
    pending_result_ = std::move(result);
}

void CacheTransaction::add_include_dependent(const std::string& include_path) {
    if (finished_) {
        throw std::logic_error(
            "CacheTransaction::add_include_dependent called on a finished transaction");
    }
    pending_include_deps_.push_back(include_path);
}

void CacheTransaction::commit() {
    if (finished_) {
        throw std::logic_error("CacheTransaction::commit called on a finished transaction");
    }
    finished_ = true;

    // Apply include-dependency edges.
    for (const auto& include_path : pending_include_deps_) {
        cache_->add_include_dependent(include_path, uri_);
    }

    // Insert the analysis result (if one was staged).
    if (pending_result_.has_value()) {
        cache_->insert(uri_, std::move(*pending_result_));
    }
}

void CacheTransaction::rollback() noexcept {
    finished_ = true;
    pending_result_.reset();
    pending_include_deps_.clear();
}

} // namespace luma::lsp
