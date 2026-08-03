#ifndef LUMA_LSP_SIGNATURE_LABEL_HPP
#define LUMA_LSP_SIGNATURE_LABEL_HPP

#include <cstdint>
#include <string>

#include "json/json.hpp"
#include "lsp_param_utils.hpp"
#include "lsp_position_utils.hpp"

namespace luma::lsp {

// Builds per-parameter objects with character-offset labels so editors can
// highlight the active parameter within the signature label. Falls back to a
// plain-text label when a parameter substring is not found in the label.
//
// ParameterInformation.label offsets are UTF-16 code units per LSP §3.17, not
// the UTF-8 byte offsets that std::string::find()/std::string::size() produce
// directly — the two disagree for any multi-byte UTF-8 character (e.g. a
// non-ASCII parameter name or type) preceding or within a parameter, so both
// endpoints are converted via byte_offset_to_utf16_column before being
// emitted.
[[nodiscard]] inline luma::json::JsonValue::ArrayType
build_parameter_labels(const std::string& sig_label, const std::string& params_sig) {
    luma::json::JsonValue::ArrayType param_objects;
    if (params_sig.empty()) {
        return param_objects;
    }

    const auto param_list = util::split_param_list(params_sig);
    std::size_t search_from{0};
    for (const auto& param : param_list) {
        const auto pos_in_label = sig_label.find(param, search_from);
        if (pos_in_label != std::string::npos) {
            const int utf16_start = byte_offset_to_utf16_column(sig_label, pos_in_label);
            const int utf16_end =
                byte_offset_to_utf16_column(sig_label, pos_in_label + param.size());
            param_objects.emplace_back(luma::json::JsonValue::ObjectType{
                {"label", luma::json::JsonValue(luma::json::JsonValue::ArrayType{
                              luma::json::JsonValue(static_cast<int64_t>(utf16_start)),
                              luma::json::JsonValue(static_cast<int64_t>(utf16_end)),
                          })},
            });
            search_from = pos_in_label + param.size();
        } else {
            // Fallback: use the parameter string as a plain text label.
            param_objects.emplace_back(luma::json::JsonValue::ObjectType{
                {"label", luma::json::JsonValue(param)},
            });
        }
    }
    return param_objects;
}

} // namespace luma::lsp

#endif // LUMA_LSP_SIGNATURE_LABEL_HPP
