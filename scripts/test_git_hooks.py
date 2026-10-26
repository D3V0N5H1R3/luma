#!/usr/bin/env python3
"""Regression tests for the Git hooks in ``scripts/git-hooks/``.

Each hook is run end-to-end under ``sh``, exactly as Git runs it, inside a
throwaway repository. ``clang-format`` and ``clang-tidy`` are replaced by small
stub scripts on ``PATH``, so the tests need no LLVM toolchain:

* the ``clang-format`` stub fails when the text it reads on stdin contains
  ``BAD`` — proving the hook checks the *staged* blob, not the working tree;
* the ``clang-tidy`` stub fails when the file it is given contains ``TIDY_ERROR``
  and logs every file it analyses.

The tests are skipped when ``git`` or a POSIX ``sh`` is unavailable (on Windows
the ``sh`` bundled with Git for Windows is used).

Run from the repository root:
    python -m unittest discover -s scripts -p "test_git_hooks.py"
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HOOKS_DIR = REPO_ROOT / "scripts" / "git-hooks"
PRE_COMMIT = HOOKS_DIR / "pre-commit"
COMMIT_MSG = HOOKS_DIR / "commit-msg"

CLANG_FORMAT_STUB = """#!/bin/sh
if [ "$1" = "--version" ]; then
    echo "${STUB_CLANG_FORMAT_BANNER:-clang-format version 20.1.8}"
    exit 0
fi
for arg; do
    printf '%s\\n' "$arg" >> "$STUB_LOG_DIR/clang-format.log"
done
input="$(cat)"
case "$input" in
    *BAD*) echo "stub: needs formatting" >&2; exit 1 ;;
esac
exit 0
"""

CLANG_TIDY_STUB = """#!/bin/sh
status=0
for arg; do
    case "$arg" in
        -*) ;;
        *.cpp)
            printf '%s\\n' "$arg" >> "$STUB_LOG_DIR/clang-tidy.log"
            if grep -q TIDY_ERROR "$arg"; then status=1; fi
            ;;
    esac
done
exit "$status"
"""


def find_sh() -> str | None:
    """Locate a POSIX ``sh``, falling back to the one bundled with Git for Windows."""
    sh = shutil.which("sh")
    if sh is not None:
        return sh
    git = shutil.which("git")
    if git is None:
        return None
    for candidate in (
        Path(git).resolve().parent.parent / "bin" / "sh.exe",
        Path(git).resolve().parent.parent.parent / "bin" / "sh.exe",
    ):
        if candidate.is_file():
            return str(candidate)
    return None


SH = find_sh()
GIT = shutil.which("git")


@unittest.skipIf(SH is None or GIT is None, "requires git and a POSIX sh")
class GitHookTestCase(unittest.TestCase):
    """Creates a scratch repository with stub tools on ``PATH``."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        self.logs = root / "logs"
        bin_dir = root / "bin"
        for directory in (self.repo, self.logs, bin_dir):
            directory.mkdir()
        for name, body in (("clang-format", CLANG_FORMAT_STUB), ("clang-tidy", CLANG_TIDY_STUB)):
            stub = bin_dir / name
            stub.write_bytes(body.encode())
            stub.chmod(0o755)

        # Isolate the scratch repositories from the developer's Git setup: no
        # inherited GIT_* variables (GIT_DIR, GIT_INDEX_FILE, GIT_CONFIG_* ...)
        # and no system, global, or XDG config, so settings such as
        # commit.gpgsign or a global core.hooksPath cannot break the tests.
        # HOME and XDG_CONFIG_HOME cover Git versions older than 2.32, which
        # ignore GIT_CONFIG_GLOBAL.
        empty_config = root / "empty.gitconfig"
        empty_config.write_bytes(b"")
        self.env = {
            **{k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")},
            "PATH": str(bin_dir) + os.pathsep + os.environ.get("PATH", ""),
            "STUB_LOG_DIR": self.logs.as_posix(),
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": str(empty_config),
            "HOME": str(root),
            "XDG_CONFIG_HOME": str(root),
        }

        self.git("init", "-q")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "Test")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.commentChar", "#")

    def git(self, *args: str) -> str:
        result = subprocess.run(
            [GIT, *args],
            cwd=self.repo,
            env=self.env,
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout

    def write(self, rel_path: str, text: str) -> None:
        path = self.repo / rel_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode())

    def write_compile_db(
        self, build_dir: str, sources: list[str], *, windows: bool = False
    ) -> None:
        """Write a compile database in CMake's layout, one key per line."""
        entries = []
        for source in sources:
            path = f"{self.repo.as_posix()}/{source}"
            if windows:
                # CMake on Windows may emit backslashes, JSON-escaped as "\\".
                path = path.replace("/", "\\\\")
            entries.append(
                "{\n"
                f'  "directory": "{build_dir}",\n'
                f'  "command": "clang++ -c {source}",\n'
                f'  "file": "{path}",\n'
                f'  "output": "{source}.o"\n'
                "}"
            )
        self.write(f"{build_dir}/compile_commands.json", "[\n" + ",\n".join(entries) + "\n]\n")

    def run_hook(self, hook: Path, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [SH, hook.as_posix(), *args],
            cwd=self.repo,
            env=self.env,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )

    def log(self, tool: str) -> list[str]:
        path = self.logs / f"{tool}.log"
        return path.read_text().splitlines() if path.is_file() else []


class HookModeTest(unittest.TestCase):
    @unittest.skipIf(GIT is None, "requires git")
    @unittest.skipUnless((REPO_ROOT / ".git").exists(), "requires a Git checkout")
    def test_hooks_are_executable_in_the_index(self) -> None:
        # Git skips a hook without the execute bit on Linux/macOS, and restores
        # that bit from the index mode on checkout.
        for hook in (PRE_COMMIT, COMMIT_MSG):
            with self.subTest(hook=hook.name):
                result = subprocess.run(
                    [GIT, "ls-files", "--stage", "--", hook.relative_to(REPO_ROOT).as_posix()],
                    cwd=REPO_ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                self.assertTrue(result.stdout.startswith("100755 "), result.stdout)


class PreCommitTest(GitHookTestCase):
    LINES = "".join(f"int line_{i};\n" for i in range(40))

    def setUp(self) -> None:
        super().setUp()
        self.write("core/a.cpp", self.LINES)
        self.git("add", ".")
        self.git("commit", "-q", "--no-verify", "-m", "chore: initial")

    def test_clean_staged_file_passes(self) -> None:
        self.write("core/a.cpp", self.LINES + "int ok;\n")
        self.git("add", "core/a.cpp")
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 0)
        self.assertIn("--assume-filename=core/a.cpp", self.log("clang-format"))

    def test_checks_staged_content_not_working_tree(self) -> None:
        self.write("core/a.cpp", self.LINES + "BAD\n")
        self.git("add", "core/a.cpp")
        self.write("core/a.cpp", self.LINES + "fixed but not staged\n")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("core/a.cpp", result.stderr)

    def test_unstaged_changes_do_not_block(self) -> None:
        self.write("core/a.cpp", self.LINES + "int ok;\n")
        self.git("add", "core/a.cpp")
        self.write("core/a.cpp", self.LINES + "int ok;\nBAD\n")
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 0)

    def test_renamed_file_is_checked(self) -> None:
        self.git("mv", "core/a.cpp", "core/b.cpp")
        self.write("core/b.cpp", self.LINES + "BAD\n")
        self.git("add", "core/b.cpp")
        self.assertIn("R", self.git("diff", "--cached", "--name-status"))
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 1)

    def test_path_with_space_is_checked(self) -> None:
        self.write("core/my file.hpp", "BAD\n")
        self.git("add", "core/my file.hpp")
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 1)

    def test_files_outside_ci_scope_are_ignored(self) -> None:
        for rel_path in ("language-server/tests/x.cpp", "extensions/x.cpp", "external/x.hpp"):
            self.write(rel_path, "BAD\n")
            self.git("add", rel_path)
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 0)
        self.assertEqual(self.log("clang-format"), [])

    def test_nested_in_scope_file_is_checked(self) -> None:
        self.write("tests/runtime/deep/x.cpp", "BAD\n")
        self.git("add", "tests/runtime/deep/x.cpp")
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 1)

    def test_old_clang_format_is_skipped(self) -> None:
        self.env["STUB_CLANG_FORMAT_BANNER"] = "Homebrew 2 clang-format version 19.1.7"
        self.write("core/a.cpp", "BAD\n")
        self.git("add", "core/a.cpp")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 0)
        self.assertIn("version 19 found", result.stderr)

    def test_vendor_banner_with_new_version_is_used(self) -> None:
        self.env["STUB_CLANG_FORMAT_BANNER"] = "Ubuntu 2 clang-format version 20.1.2 (1ubuntu1)"
        self.write("core/a.cpp", "BAD\n")
        self.git("add", "core/a.cpp")
        self.assertEqual(self.run_hook(PRE_COMMIT).returncode, 1)

    @unittest.skipUnless(os.name == "nt", "the Visual Studio fallback is Windows-only")
    def test_falls_back_to_visual_studio_clang_format(self) -> None:
        # With no clang-format on PATH, the hook asks vswhere for the copy that
        # Visual Studio's "C++ Clang tools" component installs.
        root = Path(self._tmp.name)
        (root / "bin" / "clang-format").unlink()
        program_files = root / "Program Files (x86)"
        llvm_bin = program_files / "VS" / "VC" / "Tools" / "Llvm" / "x64" / "bin"
        llvm_bin.mkdir(parents=True)
        bundled = llvm_bin / "clang-format.exe"
        bundled.write_bytes(CLANG_FORMAT_STUB.encode())
        installer = program_files / "Microsoft Visual Studio" / "Installer"
        installer.mkdir(parents=True)
        # vswhere prints Windows paths with CRLF line endings.
        (installer / "vswhere.exe").write_bytes(
            (
                "#!/bin/sh\n"
                'for arg; do printf \'%s\\n\' "$arg" >> "$STUB_LOG_DIR/vswhere.log"; done\n'
                f"printf '%s\\r\\n' '{bundled}'\n"
            ).encode()
        )
        self.env["PROGRAMFILES(X86)"] = str(program_files)
        self.env["PROCESSOR_ARCHITECTURE"] = "AMD64"
        self.write("core/a.cpp", self.LINES + "BAD\n")
        self.git("add", "core/a.cpp")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("--assume-filename=core/a.cpp", self.log("clang-format"))
        # Every installation is searched (-latest would check only the newest),
        # and the fix-it hint names the binary by its Windows path.
        self.assertIn("-sort", self.log("vswhere"))
        self.assertNotIn("-latest", self.log("vswhere"))
        self.assertIn(r"VC\Tools\Llvm\x64\bin\clang-format.exe", self.log("vswhere"))
        self.assertIn(f'"{bundled}" -i', result.stderr)

    def test_clang_tidy_uses_preset_compile_database_and_ci_scope(self) -> None:
        self.write_compile_db("build-debug", ["core/a.cpp", "core/a_posix.cpp", "tests/t.cpp"])
        self.write("core/a.cpp", self.LINES + "int ok;\n")
        self.write("core/a_posix.cpp", "int posix;\n")
        self.write("core/a_win32.cpp", "int win;\n")
        self.write("core/unlisted.cpp", "int unlisted;\n")
        self.write("tests/t.cpp", "int t;\n")
        self.git("add", ".")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 0, result.stderr)
        # tests/ is outside the tidy scope and *_win32.cpp is excluded, as in CI;
        # a file the database does not list is skipped with a note.
        self.assertEqual(sorted(self.log("clang-tidy")), ["core/a.cpp", "core/a_posix.cpp"])
        self.assertIn("core/unlisted.cpp is not in the compile database", result.stderr)

    def test_clang_tidy_matches_windows_compile_database(self) -> None:
        # A Windows build tree (backslash paths) lists *_win32.cpp but not
        # *_posix.cpp, which clang-tidy would mis-parse with default flags.
        self.write_compile_db("build-ninja", ["core/a.cpp", "core/a_win32.cpp"], windows=True)
        self.write("core/a.cpp", self.LINES + "int ok;\n")
        self.write("core/a_posix.cpp", "int posix;\n")
        self.git("add", "core/a.cpp", "core/a_posix.cpp")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log("clang-tidy"), ["core/a.cpp"])
        self.assertIn("core/a_posix.cpp is not in the compile database", result.stderr)

    def test_clang_tidy_failure_blocks(self) -> None:
        self.write_compile_db("build", ["core/a.cpp"])
        self.write("core/a.cpp", self.LINES + "TIDY_ERROR\n")
        self.git("add", "core/a.cpp")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 1)
        self.assertIn("clang-tidy found issues", result.stderr)

    def test_clang_tidy_skips_file_with_unstaged_changes(self) -> None:
        self.write_compile_db("build", ["core/a.cpp"])
        self.write("core/a.cpp", self.LINES + "int ok;\n")
        self.git("add", "core/a.cpp")
        self.write("core/a.cpp", self.LINES + "TIDY_ERROR\n")
        result = self.run_hook(PRE_COMMIT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("unstaged changes", result.stderr)
        self.assertEqual(self.log("clang-tidy"), [])


class CommitMsgTest(GitHookTestCase):
    def check(self, message: str) -> subprocess.CompletedProcess[str]:
        path = Path(self._tmp.name) / "COMMIT_EDITMSG"
        path.write_bytes(message.encode())
        return self.run_hook(COMMIT_MSG, path.as_posix())

    def test_accepted_subjects(self) -> None:
        for subject in (
            "feat: add match expression",
            "fix(lexer): handle string escape",
            "refactor!: drop legacy include resolver",
            "style: apply clang-format 18",
            "ci: pin runner image",
            "Merge branch 'main' into feature/x",
            "Merge pull request #12 from user/branch",
            "Merge remote-tracking branch 'origin/main'",
            'Revert "feat: add thing"',
            'Reapply "feat: add thing"',
            "fixup! feat: add thing",
            "amend! fix: thing",
        ):
            with self.subTest(subject=subject):
                result = self.check(subject + "\n")
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejected_subjects(self) -> None:
        for subject in (
            "added a thing",
            "feature: add thing",
            "feat:   ",
            "feat:add thing",
            "Merge whatever I like",
            "merge: branch",
        ):
            with self.subTest(subject=subject):
                self.assertEqual(self.check(subject + "\n").returncode, 1)

    def test_rejected_subject_is_echoed_verbatim(self) -> None:
        # dash's echo would expand "\c" (truncating the line) and "\t".
        subject = r"added C:\tmp\code path"
        result = self.check(subject + "\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn(f"  Subject: {subject}\n", result.stderr)

    def test_comments_and_leading_blank_lines_are_skipped(self) -> None:
        self.assertEqual(self.check("\n# comment\nfeat: real subject\n").returncode, 0)

    def test_indented_hash_is_not_a_comment(self) -> None:
        # Git only strips comment lines that start in column 0.
        self.assertEqual(self.check("  # kept by git\nfeat: x\n").returncode, 1)

    def test_custom_comment_char(self) -> None:
        self.git("config", "core.commentChar", ";")
        self.assertEqual(self.check("; comment\nfeat: real subject\n").returncode, 0)

    def test_crlf_message(self) -> None:
        self.assertEqual(self.check("fix: crlf subject\r\n\r\nbody\r\n").returncode, 0)

    def test_text_below_scissors_is_ignored(self) -> None:
        message = "# ------------------------ >8 ------------------------\ndiff --git a/x b/x\n"
        self.assertEqual(self.check(message).returncode, 0)

    def test_length_warning_counts_characters(self) -> None:
        within = self.check("feat: " + "é" * 66 + "\n")
        self.assertEqual(within.returncode, 0)
        self.assertNotIn("Warning", within.stderr)
        over = self.check("feat: " + "é" * 67 + "\n")
        self.assertEqual(over.returncode, 0)
        self.assertIn("73 characters", over.stderr)


if __name__ == "__main__":
    unittest.main()
