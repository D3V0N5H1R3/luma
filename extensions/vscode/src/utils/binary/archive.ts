// Archive extraction (.zip via PowerShell/unzip, .tar.gz via tar).
// Part of the binary-download module (see ../binary-download.ts).

import { isWindows } from "./platform";

/**
 * Escapes a path for use inside a PowerShell single-quoted string literal.
 *
 * PowerShell single-quoted strings are literal — `$`, backslashes, and other
 * metacharacters are NOT interpolated, so only the `'` terminator needs to be
 * doubled. Backticks and null bytes are rejected defensively: a backtick could
 * be mistaken for an escape sequence if a caller ever switches to a
 * double-quoted string, and a null byte would truncate the command.
 */
export function escapePowerShellLiteral(value: string): string {
    if (/[`\0]/.test(value)) {
        throw new Error(`Path contains unsafe characters for PowerShell: ${value}`);
    }
    return value.replaceAll("'", "''");
}

/**
 * Builds the PowerShell command that unpacks a `.zip` archive.
 *
 * `-LiteralPath` is required: `-Path` treats `[`, `]`, `*`, and `?` as
 * wildcards, so a storage path containing brackets (e.g. a user name) would
 * match nothing and the extraction would fail.
 */
export function buildExpandArchiveCommand(archive_path: string, dest_dir: string): string {
    const safe_archive = escapePowerShellLiteral(archive_path);
    const safe_dest = escapePowerShellLiteral(dest_dir);
    return `Expand-Archive -LiteralPath '${safe_archive}' -DestinationPath '${safe_dest}' -Force`;
}

export async function extractArchive(archive_path: string, dest_dir: string): Promise<void> {
    const { execFile } = await import("node:child_process");
    const { promisify } = await import("node:util");
    const exec = promisify(execFile);

    if (archive_path.endsWith(".zip")) {
        if (isWindows()) {
            await exec("powershell", [
                "-NoProfile",
                "-Command",
                buildExpandArchiveCommand(archive_path, dest_dir),
            ]);
        } else {
            await exec("unzip", ["-o", archive_path, "-d", dest_dir]);
        }
    } else {
        // .tar.gz
        await exec("tar", ["-xzf", archive_path, "-C", dest_dir]);
    }
}
