#include "stdlib/stdlib_catalog_internal.hpp"

namespace luma::stdlib::detail {

void register_encoder_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                const ParamShorthands& p) {
    append_specs(
        specs,
        {
            m.fn("decode_base64", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("decode_base64_typed", 1, "(value: string)",
                 R::result(R::string_type(), named::encoder_error()), {p.string}),
            m.fn("decode_base64url", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("decode_text", 2, "(bytes: array<integer>, encoding: Encoder.Encoding)",
                 R::result_string(), {p.array_any, named::encoding()}),
            m.fn("decode_text_typed", 2, "(bytes: array<integer>, encoding: Encoder.Encoding)",
                 R::result(R::string_type(), named::encoder_error()),
                 {p.array_any, named::encoding()}),
            m.fn("decode_url", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("decode_url_typed", 1, "(value: string)",
                 R::result(R::string_type(), named::encoder_error()), {p.string}),
            m.fn("encode_base64", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("encode_base64url", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("encode_text", 2, "(text: string, encoding: Encoder.Encoding)",
                 R::result(R::array_integer()), {p.string, named::encoding()}),
            m.fn("encode_url", 1, "(value: string)", R::result_string(), {p.string}),
            m.fn("is_valid_base64", 1, "(value: string)", R::boolean_type(), {p.string}),
            m.fn("is_valid_utf8", 1, "(bytes: array<integer>)", R::boolean_type(), {p.array_any}),
        });
}

void register_hash_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                             const ParamShorthands& p) {
    append_specs(specs,
                 {
                     m.fn("algorithms", 0, "()", R::array_string(), {}),
                     m.fn("crc32", 1, "(value: string)", R::integer_type(), {p.string}),
                     m.fn("digest", 2, "(algorithm: Hash.Algorithm | string, input: string)",
                          R::string_type(), {p.any, p.string}),
                     m.fn("hmac_sha256", 2, "(key: string, value: string)", R::string_type(),
                          {p.string, p.string}),
                     m.fn("hmac_sha512", 2, "(key: string, value: string)", R::string_type(),
                          {p.string, p.string}),
                     m.fn("md5", 1, "(value: string)", R::string_type(), {p.string}),
                     m.fn("md5_typed", 1, "(value: string)", named::digest(), {p.string}),
                     m.fn("sha1", 1, "(value: string)", R::string_type(), {p.string}),
                     m.fn("sha1_typed", 1, "(value: string)", named::digest(), {p.string}),
                     m.fn("sha256", 1, "(value: string)", R::string_type(), {p.string}),
                     m.fn("sha256_file", 1, "(path: string)", R::result_string(), {p.string}),
                     m.fn("sha256_typed", 1, "(value: string)", named::digest(), {p.string}),
                     m.fn("sha512", 1, "(value: string)", R::string_type(), {p.string}),
                     m.fn("sha512_file", 1, "(path: string)", R::result_string(), {p.string}),
                     m.fn("sha512_typed", 1, "(value: string)", named::digest(), {p.string}),
                     m.fn("verify", 3,
                          "(algorithm: Hash.Algorithm | string, input: string, expected: string)",
                          R::boolean_type(), {p.any, p.string, p.string}),
                 });
}

void register_compression_functions(std::vector<FunctionSpec>& specs, const ModuleBuilder& m,
                                    const ParamShorthands& p) {
    append_specs(specs,
                 {
                     m.fn("compress", 2, "(data: string, format: Compression.Format)",
                          R::string_type(), {p.string, named::compression_format()}),
                     m.fn("compressed_size", 1, "(data: string)", R::integer_type(), {p.string}),
                     m.fn("decode_rle", 1, "(data: string)", R::result_string(), {p.string}),
                     m.fn("decompress", 2, "(data: string, format: Compression.Format)",
                          R::result_string(), {p.string, named::compression_format()}),
                     m.fn("decompress_typed", 2, "(data: string, format: Compression.Format)",
                          R::result(R::string_type(), named::compression_error()),
                          {p.string, named::compression_format()}),
                     m.fn("deflate", 1, "(data: string)", R::string_type(), {p.string}),
                     m.fn("deflate_with", 2, "(data: string, level: integer)", R::result_string(),
                          {p.string, p.integer}),
                     m.fn("encode_rle", 1, "(data: string)", R::string_type(), {p.string}),
                     m.fn("gunzip", 1, "(data: string)", R::result_string(), {p.string}),
                     m.fn("gunzip_file", 1, "(path: string)", R::result_string(), {p.string}),
                     m.fn("gunzip_typed", 1, "(data: string)",
                          R::result(R::string_type(), named::compression_error()), {p.string}),
                     m.fn("gzip", 1, "(data: string)", R::string_type(), {p.string}),
                     m.fn("gzip_file", 2, "(path: string, output: string)", R::result_string(),
                          {p.string, p.string}),
                     m.fn("gzip_file_with", 3, "(path: string, output: string, level: integer)",
                          R::result_string(), {p.string, p.string, p.integer}),
                     m.fn("gzip_with", 2, "(data: string, level: integer)", R::result_string(),
                          {p.string, p.integer}),
                     m.fn("inflate", 1, "(data: string)", R::result_string(), {p.string}),
                     m.fn("inflate_typed", 1, "(data: string)",
                          R::result(R::string_type(), named::compression_error()), {p.string}),
                     m.fn("zlib_compress", 1, "(data: string)", R::string_type(), {p.string}),
                     m.fn("zlib_compress_with", 2, "(data: string, level: integer)",
                          R::result_string(), {p.string, p.integer}),
                     m.fn("zlib_decompress", 1, "(data: string)", R::result_string(), {p.string}),
                     m.fn("zlib_decompress_typed", 1, "(data: string)",
                          R::result(R::string_type(), named::compression_error()), {p.string}),
                     m.fn("detect_format", 1, "(data: string)",
                          R::optional(named::compression_format()), {p.string}),
                 });
}

} // namespace luma::stdlib::detail
