// AUTO-GENERATED from extensions/shared/platform-map.json
// Do not edit manually. Run: python generate-platform-code.py --vscode

export const PLATFORM_MAP: Record<string, Record<string, string>> = {
    "linux": {
        "x86_64": "linux-x86_64.tar.gz",
        "aarch64": "linux-aarch64.tar.gz",
    },
    "macos": {
        "x86_64": "macos-x86_64.tar.gz",
        "aarch64": "macos-aarch64.tar.gz",
    },
    "windows": {
        "x86_64": "windows-x86_64.zip",
        "aarch64": "windows-aarch64.zip",
    },
};

/** Maps Node.js process.platform to canonical OS name. */
export const OS_MAP: Record<string, string> = {
    linux: "linux",
    darwin: "macos",
    win32: "windows",
};

/** Maps Node.js process.arch to canonical architecture name. */
export const ARCH_MAP: Record<string, string> = {
    x64: "x86_64",
    arm64: "aarch64",
};

/** Get the platform-specific archive suffix for the current platform. */
export function getPlatformSuffix(): string | undefined {
    const os = OS_MAP[process.platform];
    const arch = ARCH_MAP[process.arch];
    return os && arch ? PLATFORM_MAP[os]?.[arch] : undefined;
}
