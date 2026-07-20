#ifndef LUMA_STDLIB_HTTP_MODULE_HPP
#define LUMA_STDLIB_HTTP_MODULE_HPP

// Http module — public header.
//
// The module is split across several files for readability:
//   http_module.cpp              — module registration
//   http_module_request.cpp      — connection management, request building,
//                                  response parsing, and request execution
//   http_module_parsing.cpp      — URL parsing, encoding, query string
//                                  operations, and authentication helpers
//   http_security.cpp            — SSRF prevention, CRLF injection detection,
//                                  and hostname validation

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_http_ns(const EnvPtr& env);

// Internal sub-registration (split for readability).
void register_http_parsing(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_HTTP_MODULE_HPP
