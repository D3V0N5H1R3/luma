#!/usr/bin/env python3
"""Unit tests for the shared binary downloader (``binary-download/download.py``).

The downloader is documented as a deprecated reference implementation, but its
correctness-critical pieces still have real value and no other coverage:

* ``find_asset_hash`` — the SHA256SUMS parser, cross-checked here against the
  same golden fixture (``sha256sums-sample.txt``) the VS Code and Zed parsers
  use so all implementations stay in agreement.
* ``_safe_extract_zip`` / ``_safe_extract_tar`` — the path-traversal guards
  that stop a malicious archive from writing outside the target directory.
* ``verify_checksum`` and ``get_asset_suffix`` — digest comparison and the
  platform→suffix lookup.

The test file lives in ``extensions/shared/`` (not the hyphenated
``binary-download/`` subdirectory, which is not an importable package) so
``python -m unittest discover`` picks it up; the module under test is loaded by
path.

Run:
    python -m unittest test_download
"""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

_SHARED_DIR = Path(__file__).resolve().parent
_SCRIPT = _SHARED_DIR / "binary-download" / "download.py"
_spec = importlib.util.spec_from_file_location("luma_download", _SCRIPT)
assert _spec and _spec.loader
download = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(download)


@contextlib.contextmanager
def _quiet():
    """Swallow the JSON progress events download.py prints to stdout."""
    with contextlib.redirect_stdout(io.StringIO()):
        yield


class FindAssetHashFixture(unittest.TestCase):
    """Cross-check the parser against the shared golden fixture."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = (_SHARED_DIR / "sha256sums-sample.txt").read_text(encoding="utf-8")

    def test_double_space_separator(self) -> None:
        self.assertEqual(
            download.find_asset_hash(self.manifest, "luma_lsp-linux-x86_64.tar.gz"),
            "1" * 64,
        )

    def test_single_space_separator(self) -> None:
        self.assertEqual(
            download.find_asset_hash(self.manifest, "luma_lsp-macos-aarch64.tar.gz"),
            "2" * 64,
        )

    def test_binary_mode_star_marker_is_stripped(self) -> None:
        # Regression guard: the previous parser compared the raw "*name" token
        # and so never matched binary-mode entries.
        self.assertEqual(
            download.find_asset_hash(self.manifest, "luma_dap-windows-x86_64.zip"),
            "3" * 64,
        )

    def test_uppercase_digest_normalises_to_lowercase(self) -> None:
        self.assertEqual(
            download.find_asset_hash(self.manifest, "luma-linux-x86_64.tar.gz"),
            "a" * 64,
        )

    def test_short_digest_is_ignored(self) -> None:
        self.assertIsNone(download.find_asset_hash(self.manifest, "ignored-short-hash.tar.gz"))

    def test_missing_asset_returns_none(self) -> None:
        self.assertIsNone(download.find_asset_hash(self.manifest, "not-present.tar.gz"))


class FindAssetHashUnits(unittest.TestCase):
    def test_comment_line_is_ignored(self) -> None:
        manifest = "# luma-linux-x86_64.tar.gz\n" + ("f" * 64) + "  luma-linux-x86_64.tar.gz\n"
        self.assertEqual(download.find_asset_hash(manifest, "luma-linux-x86_64.tar.gz"), "f" * 64)

    def test_empty_manifest_returns_none(self) -> None:
        self.assertIsNone(download.find_asset_hash("", "anything"))


class VerifyChecksum(unittest.TestCase):
    def test_matching_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "blob.bin"
            path.write_bytes(b"hello luma")
            digest = hashlib.sha256(b"hello luma").hexdigest()
            with _quiet():
                # Digest comparison is case-insensitive.
                self.assertTrue(download.verify_checksum(path, digest.upper()))

    def test_mismatching_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "blob.bin"
            path.write_bytes(b"hello luma")
            with _quiet():
                self.assertFalse(download.verify_checksum(path, "0" * 64))


class GetAssetSuffix(unittest.TestCase):
    PLATFORM_MAP = {"linux": {"x86_64": "linux-x86_64.tar.gz"}}

    def test_known_platform(self) -> None:
        self.assertEqual(
            download.get_asset_suffix(self.PLATFORM_MAP, "linux", "x86_64"),
            "linux-x86_64.tar.gz",
        )

    def test_unknown_platform_exits_with_code_3(self) -> None:
        with _quiet(), self.assertRaises(SystemExit) as ctx:
            download.get_asset_suffix(self.PLATFORM_MAP, "solaris", "sparc")
        self.assertEqual(ctx.exception.code, 3)


class SafeExtractZip(unittest.TestCase):
    def test_safe_member_extracts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            archive = tmp_path / "ok.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("sub/ok.txt", "data")

            dest = tmp_path / "out"
            dest.mkdir()
            download._safe_extract_zip(archive, dest)
            self.assertTrue((dest / "sub" / "ok.txt").is_file())

    def test_path_traversal_member_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            archive = tmp_path / "evil.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("../evil.txt", "pwned")

            dest = tmp_path / "out"
            dest.mkdir()
            with self.assertRaises(ValueError):
                download._safe_extract_zip(archive, dest)
            self.assertFalse((tmp_path / "evil.txt").exists())


class SafeExtractTar(unittest.TestCase):
    @staticmethod
    def _write_tar(path: Path, member_name: str, data: bytes = b"data") -> None:
        with tarfile.open(path, "w:gz") as tf:
            info = tarfile.TarInfo(name=member_name)
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))

    def test_safe_member_extracts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            archive = tmp_path / "ok.tar.gz"
            self._write_tar(archive, "sub/ok.txt")

            dest = tmp_path / "out"
            dest.mkdir()
            download._safe_extract_tar(archive, dest)
            self.assertTrue((dest / "sub" / "ok.txt").is_file())

    def test_path_traversal_member_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            archive = tmp_path / "evil.tar.gz"
            self._write_tar(archive, "../evil.txt", b"pwned")

            dest = tmp_path / "out"
            dest.mkdir()
            with self.assertRaises(ValueError):
                download._safe_extract_tar(archive, dest)
            self.assertFalse((tmp_path / "evil.txt").exists())


class RejectUnsafeMember(unittest.TestCase):
    """Direct coverage for the shared path-traversal guard both extractors use."""

    def test_member_inside_dest_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest_real = Path(tmp).resolve()
            download._reject_unsafe_member(dest_real / "sub" / "ok.txt", dest_real, "sub/ok.txt")

    def test_dest_root_itself_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest_real = Path(tmp).resolve()
            download._reject_unsafe_member(dest_real, dest_real, ".")

    def test_traversal_member_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest_real = Path(tmp).resolve()
            target = (dest_real / ".." / "evil.txt").resolve()
            with self.assertRaises(ValueError) as ctx:
                download._reject_unsafe_member(target, dest_real, "../evil.txt")
            self.assertIn("../evil.txt", str(ctx.exception))


class ExtractArchiveDispatch(unittest.TestCase):
    def _assert_dispatch(self, name: str, expected: str) -> None:
        """Assert extract_archive routes ``name`` to the ``expected`` extractor."""
        archive = Path(name)
        dest = Path("out")
        with (
            mock.patch.object(download, "_safe_extract_zip") as extract_zip,
            mock.patch.object(download, "_safe_extract_tar") as extract_tar,
            _quiet(),
        ):
            download.extract_archive(archive, dest)
        extractors = {"zip": extract_zip, "tar": extract_tar}
        extractors.pop(expected).assert_called_once_with(archive, dest)
        for unused in extractors.values():
            unused.assert_not_called()

    def test_zip_suffix_uses_zip_extractor(self) -> None:
        self._assert_dispatch("luma.zip", "zip")

    def test_tar_gz_suffix_uses_tar_extractor(self) -> None:
        self._assert_dispatch("luma.tar.gz", "tar")

    def test_tgz_suffix_uses_tar_extractor(self) -> None:
        self._assert_dispatch("luma.tgz", "tar")

    def test_unknown_format_exits_with_code_1(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "mystery.rar"
            archive.write_bytes(b"not an archive")
            with _quiet(), self.assertRaises(SystemExit) as ctx:
                download.extract_archive(archive, Path(tmp))
            self.assertEqual(ctx.exception.code, 1)


class _FakeResponse:
    """Minimal stand-in for the object urllib.request.urlopen yields.

    Supports use as a context manager plus ``read()`` (whole-body or chunked)
    and a ``headers`` mapping, which is all download.py touches.
    """

    def __init__(self, body: bytes = b"", *, headers: dict | None = None):
        self._body = body
        self.headers = headers or {}
        self._pos = 0

    def __enter__(self) -> _FakeResponse:
        return self

    def __exit__(self, *_exc) -> bool:
        return False

    def read(self, size: int = -1) -> bytes:
        if size is None or size < 0:
            data = self._body[self._pos :]
            self._pos = len(self._body)
            return data
        data = self._body[self._pos : self._pos + size]
        self._pos += len(data)
        return data


class DetectPlatform(unittest.TestCase):
    def test_maps_supported_platforms(self) -> None:
        cases = [
            ("Linux", "x86_64", ("linux", "x86_64")),
            ("Linux", "aarch64", ("linux", "aarch64")),
            ("Darwin", "arm64", ("macos", "aarch64")),
            ("Darwin", "x86_64", ("macos", "x86_64")),
            ("Windows", "AMD64", ("windows", "x86_64")),
        ]
        for system, machine, expected in cases:
            with (
                self.subTest(system=system, machine=machine),
                mock.patch.object(download.platform, "system", return_value=system),
                mock.patch.object(download.platform, "machine", return_value=machine),
            ):
                self.assertEqual(download.detect_platform(), expected)

    def test_unsupported_platform_exits_3(self) -> None:
        with (
            mock.patch.object(download.platform, "system", return_value="Plan9"),
            mock.patch.object(download.platform, "machine", return_value="sparc"),
            _quiet(),
            self.assertRaises(SystemExit) as ctx,
        ):
            download.detect_platform()
        self.assertEqual(ctx.exception.code, 3)


class EmitProgress(unittest.TestCase):
    def test_emits_single_json_line(self) -> None:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            download.emit_progress("download_progress", "half", percent=50)
        payload = json.loads(buffer.getvalue().strip())
        self.assertEqual(payload, {"event": "download_progress", "message": "half", "percent": 50})


class FetchJson(unittest.TestCase):
    def test_decodes_response_body(self) -> None:
        response = _FakeResponse(json.dumps({"tag_name": "v1.2.3"}).encode("utf-8"))
        with mock.patch("urllib.request.urlopen", return_value=response):
            self.assertEqual(download.fetch_json("https://example/api"), {"tag_name": "v1.2.3"})


class FetchRelease(unittest.TestCase):
    _URLS = {
        "api_latest": "https://api/latest",
        "api_tagged": "https://api/tags/{tag}",
    }

    def test_latest_uses_latest_endpoint(self) -> None:
        with (
            mock.patch.object(download, "fetch_json", return_value={"ok": True}) as fetch,
            _quiet(),
        ):
            self.assertEqual(download.fetch_release("latest", self._URLS), {"ok": True})
        fetch.assert_called_once_with("https://api/latest")

    def test_tagged_formats_endpoint(self) -> None:
        with (
            mock.patch.object(download, "fetch_json", return_value={"ok": True}) as fetch,
            _quiet(),
        ):
            download.fetch_release("v9.9.9", self._URLS)
        fetch.assert_called_once_with("https://api/tags/v9.9.9")

    def test_fetch_failure_exits_1(self) -> None:
        with (
            mock.patch.object(download, "fetch_json", side_effect=OSError("network down")),
            _quiet(),
            self.assertRaises(SystemExit) as ctx,
        ):
            download.fetch_release("latest", self._URLS)
        self.assertEqual(ctx.exception.code, 1)


class DownloadFile(unittest.TestCase):
    def test_writes_chunked_body_to_destination(self) -> None:
        body = b"luma-binary-payload" * 10000  # exceeds one 64 KiB chunk
        response = _FakeResponse(body, headers={"Content-Length": str(len(body))})
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "luma.tar.gz"
            with mock.patch("urllib.request.urlopen", return_value=response), _quiet():
                download.download_file("https://example/luma.tar.gz", dest)
            self.assertEqual(dest.read_bytes(), body)

    def test_handles_missing_content_length(self) -> None:
        body = b"tiny"
        response = _FakeResponse(body, headers={})
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "luma"
            with mock.patch("urllib.request.urlopen", return_value=response), _quiet():
                download.download_file("https://example/luma", dest)
            self.assertEqual(dest.read_bytes(), body)


class FetchExpectedChecksum(unittest.TestCase):
    _MANIFEST = f"{'1' * 64}  luma-linux-x86_64.tar.gz\n{'2' * 64} *luma-macos-aarch64.tar.gz\n"

    def _release(self) -> dict:
        return {
            "assets": [
                {
                    "name": "SHA256SUMS",
                    "browser_download_url": "https://example/SHA256SUMS",
                },
            ]
        }

    def test_returns_hash_for_present_asset(self) -> None:
        response = _FakeResponse(self._MANIFEST.encode("utf-8"))
        with mock.patch("urllib.request.urlopen", return_value=response), _quiet():
            digest = download.fetch_expected_checksum(
                self._release(), "luma-macos-aarch64.tar.gz", "SHA256SUMS"
            )
        self.assertEqual(digest, "2" * 64)

    def test_missing_entry_returns_none(self) -> None:
        response = _FakeResponse(self._MANIFEST.encode("utf-8"))
        with mock.patch("urllib.request.urlopen", return_value=response), _quiet():
            digest = download.fetch_expected_checksum(
                self._release(), "luma-windows-x86_64.zip", "SHA256SUMS"
            )
        self.assertIsNone(digest)

    def test_absent_checksums_asset_returns_none(self) -> None:
        release = {"assets": [{"name": "luma-linux-x86_64.tar.gz", "browser_download_url": "u"}]}
        with _quiet():
            digest = download.fetch_expected_checksum(
                release, "luma-linux-x86_64.tar.gz", "SHA256SUMS"
            )
        self.assertIsNone(digest)


class MakeExecutable(unittest.TestCase):
    def _fake_path(self) -> mock.MagicMock:
        path = mock.MagicMock(spec=Path)
        path.stat.return_value = SimpleNamespace(st_mode=0o644)
        return path

    def test_sets_exec_bits_on_posix(self) -> None:
        path = self._fake_path()
        with mock.patch.object(download.os, "name", "posix"):
            download.make_executable(path)
        path.chmod.assert_called_once_with(0o644 | 0o755)

    def test_noop_on_windows(self) -> None:
        path = self._fake_path()
        with mock.patch.object(download.os, "name", "nt"):
            download.make_executable(path)
        path.chmod.assert_not_called()


if __name__ == "__main__":
    unittest.main()
