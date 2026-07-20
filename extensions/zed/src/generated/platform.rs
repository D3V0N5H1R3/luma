// AUTO-GENERATED from extensions/shared/platform-map.json
// Do not edit manually. Run: python generate-platform-code.py --zed

/// Look up the archive suffix for a given OS and architecture.
pub fn platform_suffix_for(os: &str, arch: &str) -> Option<&'static str> {
    match (os, arch) {
        ("linux", "x86_64") => Some("linux-x86_64.tar.gz"),
        ("linux", "aarch64") => Some("linux-aarch64.tar.gz"),
        ("macos", "x86_64") => Some("macos-x86_64.tar.gz"),
        ("macos", "aarch64") => Some("macos-aarch64.tar.gz"),
        ("windows", "x86_64") => Some("windows-x86_64.zip"),
        ("windows", "aarch64") => Some("windows-aarch64.zip"),
        _ => None,
    }
}
