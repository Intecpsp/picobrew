import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import usb_stock


class ZymaticStockTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.data = self.root / "hub"
        # A tiny test image exercises storage without packaging the vendor firmware.
        self.contents = b":020000000102FB\r\n:00000001FF\r\n"
        digest = patch.object(usb_stock, "SHA256", hashlib.sha256(self.contents).hexdigest())
        digest.start()
        self.addCleanup(digest.stop)

    def test_accepts_original_then_validates_cache_on_each_use(self):
        source = self.root / "download.hex"
        source.write_bytes(self.contents)
        path = usb_stock.prepare(self.data, source)
        self.assertEqual(path.name, usb_stock.FILENAME)
        self.assertEqual(path.read_bytes(), self.contents)
        usb_stock.private_files.assert_private(path)
        source.unlink()
        self.assertEqual(usb_stock.prepare(self.data), path)
        path.write_bytes(self.contents.replace(b"\r", b""))
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            usb_stock.prepare(self.data)

    def test_zip_preserves_original_bytes_and_idempotent_import(self):
        source = self.root / "vendor.zip"
        with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("Zymatic/Readme.txt", "OEM")
            archive.writestr("Zymatic/Firmware.hex", self.contents)
        expected = usb_stock.prepare(self.data, source)
        self.assertEqual(usb_stock.prepare(self.data, source), expected)
        self.assertEqual(expected.read_bytes(), self.contents)
        self.assertEqual(list(expected.parent.iterdir()), [expected])

    def test_ambiguous_or_oversized_archive_creates_no_cache(self):
        source = self.root / "vendor.zip"
        with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("first.hex", self.contents)
            archive.writestr("second.hex", self.contents)
        with self.assertRaisesRegex(ValueError, "one firmware HEX"):
            usb_stock.prepare(self.data, source)
        with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("huge.hex", bytes(usb_stock.MAX_HEX + 1))
        with self.assertRaisesRegex(ValueError, "oversized"):
            usb_stock.prepare(self.data, source)
        self.assertFalse(self.data.exists())

    def test_missing_held_or_substituted_firmware(self):
        with patch.object(usb_stock, "fetch_source", side_effect=OSError("offline")), self.assertRaisesRegex(OSError, "offline"):
            usb_stock.prepare(self.data)
        source = self.root / usb_stock.FILENAME
        source.write_bytes(self.contents.replace(b"\r", b""))
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            usb_stock.prepare(self.data, source)
        self.assertFalse(self.data.exists())

    def test_pinned_download_verifies_archive_then_hex_and_prefers_cache(self):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("vendor.hex", self.contents)
        raw = data.getvalue()
        with patch.object(usb_stock, "ARCHIVE_SIZE", len(raw)), patch.object(
                usb_stock, "ARCHIVE_SHA256", hashlib.sha256(raw).hexdigest()), patch(
                    "urllib.request.urlopen", side_effect=[io.BytesIO(raw + b"!"), io.BytesIO(raw)]) as get:
            with self.assertRaisesRegex(ValueError, "pinned original"):
                usb_stock.prepare(self.data)
            self.assertFalse(self.data.exists())
            path = usb_stock.prepare(self.data)
            self.assertEqual(usb_stock.prepare(self.data), path)
            self.assertEqual(get.call_count, 2)
            get.assert_called_with(usb_stock.ARCHIVE_URL, timeout=20)
            self.assertEqual(path.read_bytes(), self.contents)

    def test_decoder_checks_contiguity_checksum_and_decoded_identity(self):
        with patch.object(usb_stock, "APP_SIZE", 2), patch.object(
                usb_stock, "APP_SHA256", hashlib.sha256(bytes.fromhex("0102")).hexdigest()):
            self.assertEqual(usb_stock.decode_application(self.contents), bytes.fromhex("0102"))
            with patch.object(usb_stock, "verify"):
                for bad in [self.contents.replace(b"FB", b"FC"), self.contents.replace(
                        b"020000000102FB", b"020001000102FA"), self.contents.splitlines()[0],
                        self.contents.replace(b"0102FB", b"0202FA")]:
                    with self.assertRaises(ValueError):
                        usb_stock.decode_application(bad)


if __name__ == "__main__":
    unittest.main()
