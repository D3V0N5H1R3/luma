#ifndef LUMA_STDLIB_BINARYTREE_INTERNAL_HPP
#define LUMA_STDLIB_BINARYTREE_INTERNAL_HPP

// Internal BST helper primitives shared by the binarytree_*.cpp registration
// units.  Not part of the public stdlib surface — include only from
// binarytree_*.cpp translation units.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <queue>
#include <span>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/value_compare.hpp"
#include "runtime/stdlib/common/error_messages.hpp"

namespace luma::binarytree_detail {

// Maximum recursion depth for tree operations.
inline const int max_tree_depth = ResourceLimits::max_call_depth;

// BST comparison wrapper — delegates to the shared compare_values helper.
[[nodiscard]] inline int bst_compare(const Value& a, const Value& b, const SourceLocation& loc) {
    return compare_values(a, b, loc, "BinaryTree");
}

struct InsertResult {
    std::shared_ptr<BinaryTreeNode> node;
    bool inserted{false};
};

// Insert into a BST.
[[nodiscard]] inline InsertResult insert_node(const std::shared_ptr<BinaryTreeNode>& node,
                                              const Value& val, const SourceLocation& loc,
                                              int depth = 0) {
    if (depth > max_tree_depth) {
        throw RuntimeError{error_msg("BinaryTree", "insert", "maximum tree depth exceeded"), loc,
                           "the tree may be too deep or degenerate"};
    }

    if (!node) {
        return {.node = std::make_shared<BinaryTreeNode>(val), .inserted = true};
    }

    const int cmp = bst_compare(val, node->value, loc);

    if (cmp == 0) {
        // Duplicate — return original node unchanged.
        return {.node = node, .inserted = false};
    }

    if (cmp < 0) {
        auto [left, inserted] = insert_node(node->left, val, loc, depth + 1);

        if (!inserted) {
            return {.node = node, .inserted = false};
        }

        auto copy = std::make_shared<BinaryTreeNode>(node->value);
        copy->left = std::move(left);
        copy->right = node->right;

        return {.node = std::move(copy), .inserted = true};
    }

    // cmp > 0
    auto [right, inserted] = insert_node(node->right, val, loc, depth + 1);

    if (!inserted) {
        return {.node = node, .inserted = false};
    }

    auto copy = std::make_shared<BinaryTreeNode>(node->value);
    copy->left = node->left;
    copy->right = std::move(right);

    return {.node = std::move(copy), .inserted = true};
}

// Insert a value into a tree we own, updating the root spine (copy-on-write via
// insert_node) and bumping the element count only when a new value was added.
// Centralises the insert-and-count pattern shared by from_array, insert, union,
// filter, and partition.
inline void tree_insert(BinaryTreeValue& tree, const Value& val, const SourceLocation& loc) {
    auto [root, inserted] = insert_node(tree.root, val, loc);

    tree.root = std::move(root);

    if (inserted) {
        // Cap the element count like every other collection (Array, LinkedList,
        // Dictionary, Queue, Stack, Set all cap at their max_*_size).  The depth
        // guard in insert_node only bounds a *degenerate* tree — a balanced tree
        // can hold ~2^max_tree_depth nodes, so without this a loop of inserts
        // would grow the tree until memory is exhausted.
        if (tree.count_ >= ResourceLimits::max_array_size) {
            throw RuntimeError{
                error_msg("BinaryTree", "insert", "maximum tree size exceeded"), loc,
                std::format("the maximum size is {} elements", ResourceLimits::max_array_size)};
        }

        ++tree.count_;
    }
}

// Find the minimum node.
[[nodiscard]] inline std::shared_ptr<BinaryTreeNode>
find_min(const std::shared_ptr<BinaryTreeNode>& node, int depth = 0) {
    if (depth > max_tree_depth) {
        return node;
    }

    if (!node->left) {
        return node;
    }

    return find_min(node->left, depth + 1);
}

// Find the maximum node.
[[nodiscard]] inline std::shared_ptr<BinaryTreeNode>
find_max(const std::shared_ptr<BinaryTreeNode>& node, int depth = 0) {
    if (depth > max_tree_depth) {
        return node;
    }

    if (!node->right) {
        return node;
    }

    return find_max(node->right, depth + 1);
}

struct RemoveResult {
    std::shared_ptr<BinaryTreeNode> node;
    bool removed{false};
};

// Remove a value from BST.
[[nodiscard]] inline RemoveResult remove_node(const std::shared_ptr<BinaryTreeNode>& node,
                                              const Value& val, const SourceLocation& loc,
                                              int depth = 0) {
    if (depth > max_tree_depth) {
        throw RuntimeError{error_msg("BinaryTree", "remove", "maximum tree depth exceeded"), loc,
                           "the tree may be too deep or degenerate"};
    }

    if (!node) {
        return {.node = nullptr, .removed = false};
    }

    const int cmp = bst_compare(val, node->value, loc);

    if (cmp < 0) {
        auto [left, removed] = remove_node(node->left, val, loc, depth + 1);

        if (!removed) {
            return {.node = node, .removed = false};
        }

        auto copy = std::make_shared<BinaryTreeNode>(node->value);
        copy->left = std::move(left);
        copy->right = node->right;

        return {.node = std::move(copy), .removed = true};
    }

    if (cmp > 0) {
        auto [right, removed] = remove_node(node->right, val, loc, depth + 1);

        if (!removed) {
            return {.node = node, .removed = false};
        }

        auto copy = std::make_shared<BinaryTreeNode>(node->value);
        copy->left = node->left;
        copy->right = std::move(right);

        return {.node = std::move(copy), .removed = true};
    }

    // Found the node.
    if (!node->left && !node->right) {
        return {.node = nullptr, .removed = true};
    }

    if (!node->left) {
        return {.node = node->right, .removed = true};
    }

    if (!node->right) {
        return {.node = node->left, .removed = true};
    }

    // Two children — replace with in-order successor.
    const auto& successor = find_min(node->right, depth + 1);

    auto copy = std::make_shared<BinaryTreeNode>(successor->value);
    copy->left = node->left;
    copy->right = remove_node(node->right, successor->value, loc, depth + 1).node;

    return {.node = std::move(copy), .removed = true};
}

// In-order traversal.
inline void inorder(const std::shared_ptr<BinaryTreeNode>& node, std::vector<Value>& out,
                    int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return;
    }

    inorder(node->left, out, depth + 1);

    out.push_back(node->value);

    inorder(node->right, out, depth + 1);
}

// Pre-order traversal.
inline void preorder(const std::shared_ptr<BinaryTreeNode>& node, std::vector<Value>& out,
                     int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return;
    }

    out.push_back(node->value);

    preorder(node->left, out, depth + 1);
    preorder(node->right, out, depth + 1);
}

// Post-order traversal.
inline void postorder(const std::shared_ptr<BinaryTreeNode>& node, std::vector<Value>& out,
                      int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return;
    }

    postorder(node->left, out, depth + 1);
    postorder(node->right, out, depth + 1);

    out.push_back(node->value);
}

// Level-order (breadth-first) traversal.
inline void level_order(const std::shared_ptr<BinaryTreeNode>& root, std::vector<Value>& out) {
    if (!root) {
        return;
    }

    std::queue<std::shared_ptr<BinaryTreeNode>> q;
    q.push(root);

    while (!q.empty()) {
        auto cur = std::move(q.front());

        q.pop();

        out.push_back(cur->value);

        if (cur->left) {
            q.push(cur->left);
        }

        if (cur->right) {
            q.push(cur->right);
        }
    }
}

// Compute tree height.
[[nodiscard]] inline std::int64_t tree_height(const std::shared_ptr<BinaryTreeNode>& node,
                                              int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return 0;
    }

    return 1 + std::max(tree_height(node->left, depth + 1), tree_height(node->right, depth + 1));
}

// Returns the subtree height, or -1 if any node below it is height-unbalanced
// (its two subtree heights differ by more than 1).  Single post-order pass used
// by BinaryTree.is_balanced.
[[nodiscard]] inline std::int64_t balanced_height(const std::shared_ptr<BinaryTreeNode>& node,
                                                  int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return 0;
    }

    const auto left = balanced_height(node->left, depth + 1);
    if (left < 0) {
        return -1;
    }

    const auto right = balanced_height(node->right, depth + 1);
    if (right < 0) {
        return -1;
    }

    if (std::abs(left - right) > 1) {
        return -1;
    }

    return 1 + std::max(left, right);
}

// Search for a value.
[[nodiscard]] inline bool tree_contains(const std::shared_ptr<BinaryTreeNode>& node,
                                        const Value& val, const SourceLocation& loc,
                                        int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return false;
    }

    const int cmp = bst_compare(val, node->value, loc);

    if (cmp < 0) {
        return tree_contains(node->left, val, loc, depth + 1);
    }

    if (cmp > 0) {
        return tree_contains(node->right, val, loc, depth + 1);
    }

    return true;
}

// Find the largest value <= key (floor).
[[nodiscard]] inline const Value* tree_floor(const std::shared_ptr<BinaryTreeNode>& node,
                                             const Value& key, const SourceLocation& loc,
                                             int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return nullptr;
    }

    const int cmp = bst_compare(key, node->value, loc);

    if (cmp == 0) {
        return &node->value;
    }

    if (cmp < 0) {
        return tree_floor(node->left, key, loc, depth + 1);
    }

    // node->value < key: this is a candidate; check right for a better one.
    const Value* right_result = tree_floor(node->right, key, loc, depth + 1);

    return (right_result != nullptr) ? right_result : &node->value;
}

// Find the smallest value >= key (ceiling).
[[nodiscard]] inline const Value* tree_ceiling(const std::shared_ptr<BinaryTreeNode>& node,
                                               const Value& key, const SourceLocation& loc,
                                               int depth = 0) {
    if (!node || depth > max_tree_depth) {
        return nullptr;
    }

    const int cmp = bst_compare(key, node->value, loc);

    if (cmp == 0) {
        return &node->value;
    }

    if (cmp > 0) {
        return tree_ceiling(node->right, key, loc, depth + 1);
    }

    // node->value > key: this is a candidate; check left for a better one.
    const Value* left_result = tree_ceiling(node->left, key, loc, depth + 1);

    return (left_result != nullptr) ? left_result : &node->value;
}

// Smallest value strictly greater than key, or nullptr when none exists.
[[nodiscard]] inline const Value* tree_successor(const std::shared_ptr<BinaryTreeNode>& root,
                                                 const Value& key, const SourceLocation& loc) {
    const Value* result = nullptr;
    auto node = root;

    while (node) {
        if (bst_compare(key, node->value, loc) < 0) {
            result = &node->value;
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return result;
}

// Largest value strictly less than key, or nullptr when none exists.
[[nodiscard]] inline const Value* tree_predecessor(const std::shared_ptr<BinaryTreeNode>& root,
                                                   const Value& key, const SourceLocation& loc) {
    const Value* result = nullptr;
    auto node = root;

    while (node) {
        if (bst_compare(key, node->value, loc) > 0) {
            result = &node->value;
            node = node->right;
        } else {
            node = node->left;
        }
    }

    return result;
}

// Build a balanced BST from a sorted array.
[[nodiscard]] inline std::shared_ptr<BinaryTreeNode>
build_balanced(std::span<const Value> sorted, std::int64_t start, std::int64_t end, int depth = 0) {
    if (start > end || depth > max_tree_depth) {
        return nullptr;
    }

    const auto mid = start + ((end - start) / 2);
    auto node = std::make_shared<BinaryTreeNode>(sorted[static_cast<std::size_t>(mid)]);
    node->left = build_balanced(sorted, start, mid - 1, depth + 1);
    node->right = build_balanced(sorted, mid + 1, end, depth + 1);

    return node;
}

} // namespace luma::binarytree_detail

#endif // LUMA_STDLIB_BINARYTREE_INTERNAL_HPP
