"""Compile actual wrappers and the complete unmodified SDK; no 4D or release build."""

import os
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("OWNERSHIP_SOURCE_ROOT", ROOT)).resolve()


@unittest.skipUnless(sys.platform == "darwin", "macOS framework shim")
class OwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(
            prefix="jpegoptim-ownership-", dir=os.environ.get("OWNERSHIP_TMPDIR"),
        )
        cls.binary = Path(cls.temp.name) / "ownership"
        api = SOURCE / "4D Plugin API"
        sdk = (api / "4DPluginAPI.c").read_bytes()
        blob = hashlib.sha1(f"blob {len(sdk)}\0".encode() + sdk).hexdigest()
        if blob != "478081f0238f6bc5393222f574bc2656f010b67b":
            cls.temp.cleanup()
            raise AssertionError("Re-audit the changed bundled SDK before updating its pinned hash")
        command = [
            "clang++", "-std=c++14", "-x", "objective-c++",
            "-include", "Carbon/Carbon.h", "-I", str(api),
            "-I", str(api / "Classes"), "-fsanitize=address,undefined",
            "-fno-sanitize=alignment", "-fno-omit-frame-pointer", "-g",
            "-Wno-deprecated-declarations", "-Wno-return-type-c-linkage",
            "-Wno-ignored-attributes", "-Wno-writable-strings",
            str(api / "4DPluginAPI.c"),
            *map(str, sorted((api / "Classes").glob("*.cpp"))),
            str(ROOT / ".github/tests/ownership_host.mm"),
            "-framework", "Cocoa", "-framework", "Carbon",
            "-o", str(cls.binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            cls.temp.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, case, text="", empty="allocated"):
        result = subprocess.run(
            [str(self.binary), case, text, empty],
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0",
                 "UBSAN_OPTIONS": "halt_on_error=1"},
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("strings=0 pictures=0 handles=0", result.stdout)

    def test_path_single_disposal_and_retained_output(self):
        for text in ("ASCII", "\u65e5", "\U0001f600", ""):
            for empty in ("allocated", "null"):
                with self.subTest(text=text, empty=empty):
                    self.run_case("path", text, empty)

    def test_setbytes_temporary_release(self):
        self.run_case("bytes")

    def test_copy_byte_unit_boundary(self):
        for text in (".png", "\u65e5", "\U0001f600"):
            with self.subTest(text=text):
                self.run_case("copy", text, "null")

    def test_copy_initial_empty_owners(self):
        self.run_case("copy", "")

    def test_grayscale_initial_empty_owner(self):
        self.run_case("gray")

    def test_size_borrowed_input_and_empty_owner(self):
        self.run_case("size")

    def test_thumbnail_borrowed_input_and_owning_return(self):
        self.run_case("thumbnail")

    def test_array_transfers_copies_replacement_shrink_clear_rejection(self):
        for empty in ("allocated", "null"):
            with self.subTest(empty=empty):
                self.run_case("arrays", "", empty)

    def test_scalar_borrowed_copy_and_content_setter_returns(self):
        for text in ("ASCII", "\u65e5", "\U0001f600", ""):
            for empty in ("allocated", "null"):
                with self.subTest(text=text, empty=empty):
                    self.run_case("scalar", text, empty)

    def test_command_handle_failure_preserves_argument_owners(self):
        for case in ("path", "copy", "gray", "size", "thumbnail"):
            with self.subTest(case=case):
                self.run_case(case + "-handle-failure", "")


if __name__ == "__main__":
    unittest.main()
