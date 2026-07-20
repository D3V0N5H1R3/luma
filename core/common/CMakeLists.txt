# core/common/CMakeLists.txt
# Shared low-level utilities (resource limits, base64, string helpers) used by
# both the analysis front-end and the runtime back-end. Compiled as an OBJECT
# library so its translation units are embedded into luma_analysis exactly once.

luma_add_library(luma_common OBJECT
    resource_limits.cpp
    base64_codec.cpp
    string_utils.cpp
)
