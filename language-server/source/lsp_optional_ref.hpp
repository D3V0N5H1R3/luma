#ifndef LUMA_LSP_OPTIONAL_REF_HPP
#define LUMA_LSP_OPTIONAL_REF_HPP

// Convention for nullable/optional values in the LSP codebase:
//
//   std::optional<T>    — For small value types (int, string, bool) where absence is expected.
//   optional_ref<T>     — For non-owning references where the referent may not exist.
//                         Preferred over raw T* for lookup results from maps/containers.
//   const T*            — For raw non-owning pointers from C++ stdlib (e.g., map lookups
//                         via find()). Acceptable when optional_ref adds no clarity.
//   std::unique_ptr<T>  — For owned heap objects.
//
// Prefer optional_ref<T> over raw pointers for new code.

#include <optional>
#include <stdexcept>
#include <type_traits>

namespace luma::lsp {

// Lightweight non-owning optional reference.
//
// Replaces raw `const T*` returns that use nullptr for "not found" semantics.
// Provides the same pointer-like API (operator->, operator*) while clearly
// expressing that absence is an expected outcome.
//
// Const-correctness follows T: optional_ref<const X> gives read-only access,
// optional_ref<X> gives mutable access.
//
// Usage:
//     auto ref = registry.find_function("Math.floor");
//     if (ref) {
//         use(ref->return_type);   // operator-> returns T*
//     }
template <typename T> class optional_ref {
public:
    optional_ref() noexcept = default;

    optional_ref(T& ref) noexcept : ptr_{&ref} {}

    // Allow implicit conversion from optional_ref<T> to optional_ref<const T>.
    template <typename U>
        requires(!std::is_same_v<U, T> && std::is_same_v<T, const U>)
    optional_ref(optional_ref<U> other) noexcept : ptr_{other.has_value() ? &*other : nullptr} {}

    [[nodiscard]] bool has_value() const noexcept {
        return ptr_ != nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    // Checked access — throws on empty.
    [[nodiscard]] T& value() const {
        if (!ptr_) {
            throw std::bad_optional_access{};
        }
        return *ptr_;
    }

    // Unchecked access — caller must verify has_value() first.
    [[nodiscard]] T& operator*() const noexcept {
        return *ptr_;
    }

    [[nodiscard]] T* operator->() const noexcept {
        return ptr_;
    }

private:
    T* ptr_ = nullptr;
};

} // namespace luma::lsp

#endif // LUMA_LSP_OPTIONAL_REF_HPP
