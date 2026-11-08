"""Dependency downloads remain bounded and never bypass pinned checksums."""
import hashlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import urllib.error

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fetch_dxc


class DownloadTest(unittest.TestCase):
    def setUp(self):
        self.data = b'checksum-pinned compiler fixture'
        self.checksum = hashlib.sha256(self.data).hexdigest()

    def test_direct_success(self):
        with patch.object(fetch_dxc.urllib.request, 'urlopen', return_value=io.BytesIO(self.data)) as opener:
            self.assertEqual(fetch_dxc.download('fixture.zip', self.checksum, 42), self.data)
            self.assertEqual(opener.call_count, 1)

    def test_cdn_failure_uses_asset_api(self):
        failure = urllib.error.HTTPError('https://github.com/fixture', 504, 'gateway', {}, None)
        with patch.object(fetch_dxc.urllib.request, 'urlopen', side_effect=[failure, io.BytesIO(self.data)]) as opener, patch.object(fetch_dxc.time, 'sleep'):
            self.assertEqual(fetch_dxc.download('fixture.zip', self.checksum, 42), self.data)
            self.assertTrue(opener.call_args_list[1].args[0].full_url.endswith('/assets/42'))

    def test_failure_is_bounded(self):
        with patch.object(fetch_dxc.urllib.request, 'urlopen', side_effect=TimeoutError) as opener, patch.object(fetch_dxc.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'three transport attempts'):
                fetch_dxc.download('fixture.zip', self.checksum, 42)
            self.assertEqual(opener.call_count, 3)

    def test_corruption_never_retries_or_extracts(self):
        with patch.object(fetch_dxc.urllib.request, 'urlopen', return_value=io.BytesIO(b'corrupted')) as opener:
            with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                fetch_dxc.download('fixture.zip', self.checksum, 42)
            self.assertEqual(opener.call_count, 1)


if __name__ == '__main__':
    unittest.main()
