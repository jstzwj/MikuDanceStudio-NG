"""Reject mislabeled, modified or incomplete release payloads."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from release_contract import binary_arch, digest, inspect_archive, stem, verify_packages, version


class ReleaseContractTest(unittest.TestCase):
    def test_semver(self):
        self.assertEqual(version('0.0.1'), '0.0.1')
        for value in ('10.15', 'v0.0.1', '00.0.1', '0.0.1-beta', '65536.0.1'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                version(value)

    def test_native_machine_types(self):
        for machine, arch in ((0x8664, 'x64'), (0xaa64, 'arm64')):
            self.assertEqual(binary_arch(self.pe(machine)), arch)
        for machine, arch in ((62, 'x64'), (183, 'arm64')):
            elf = bytearray(32)
            elf[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', elf, 18, machine)
            self.assertEqual(binary_arch(elf), arch)
        for machine, arch in ((0x1000007, 'x64'), (0x100000c, 'arm64')):
            self.assertEqual(binary_arch(struct.pack('<II', 0xfeedfacf, machine)), arch)

    @staticmethod
    def pe(machine):
        result = bytearray(256)
        result[:2] = b'MZ'
        struct.pack_into('<I', result, 0x3c, 128)
        result[128:132] = b'PE\0\0'
        struct.pack_into('<H', result, 132, machine)
        return result

    def package(self, folder, **overrides):
        info = dict(schema=1, product='MikuDanceStudio-NG', format='portable', version='0.0.1',
                    system='Windows', arch='x64', commit='a' * 40, tag='v0.0.1',
                    development=False, modified=False)
        info.update(overrides)
        root = stem(info)
        archive = folder / (root + '.zip')
        with zipfile.ZipFile(archive, 'w') as target:
            target.writestr(root + '/build-info.json', json.dumps(info))
            target.writestr(root + '/VERSION', '0.0.1\n')
            target.writestr(root + '/MikuDanceStudioNG.exe', self.pe(0x8664))
        archive.with_name(archive.name + '.sha256').write_text(f'{digest(archive)}  {archive.name}\n')
        return archive

    def test_release_identity_and_checksum(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.package(Path(temporary))
            self.assertEqual(inspect_archive(path, 'v0.0.1', 'a' * 40)['arch'], 'x64')
            for tag, commit in (('v0.0.2', 'a' * 40), ('v0.0.1', 'b' * 40)):
                with self.assertRaises(ValueError):
                    inspect_archive(path, tag, commit)
            path.write_bytes(path.read_bytes() + b'changed')
            with self.assertRaisesRegex(ValueError, 'Checksum'):
                inspect_archive(path)

    def test_wrong_arch_or_unqualified_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            for fields in (dict(arch='arm64'), dict(development=True), dict(modified=True)):
                with self.subTest(fields=fields):
                    path = self.package(Path(temporary), **fields)
                    with self.assertRaises(ValueError):
                        inspect_archive(path, 'v0.0.1', 'a' * 40)

    def test_all_requested_platforms_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.package(folder)
            with self.assertRaisesRegex(ValueError, 'windows-arm64.*android-universal'):
                verify_packages(folder, 'v0.0.1', 'a' * 40)
            result = verify_packages(folder, 'v0.0.1', 'a' * 40, targets=('windows-x64',))
            self.assertEqual(len(result['packages']), 1)
            self.assertTrue((folder / 'SHA256SUMS').is_file())

    def test_android_universal_requires_both_native_abis(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'MikuDanceStudio-NG-v0.0.1-android-universal.apk'
            info = dict(schema=1, product='MikuDanceStudio-NG', format='apk', system='Android',
                        version='0.0.1', arch='universal', abis=['arm64-v8a', 'x86_64'],
                        commit='a' * 40, tag='v0.0.1', development=False, modified=False)
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('assets/build-info.json', json.dumps(info))
                for abi, machine in (('arm64-v8a', 183), ('x86_64', 62)):
                    elf = bytearray(32)
                    elf[:6] = b'\x7fELF\x02\x01'
                    struct.pack_into('<H', elf, 18, machine)
                    archive.writestr(f'lib/{abi}/libMikuDanceStudioNG_{abi}.so', elf)
            path.with_name(path.name + '.sha256').write_text(f'{digest(path)}  {path.name}\n')
            self.assertEqual(inspect_archive(path, 'v0.0.1', 'a' * 40)['arch'], 'universal')
            with self.assertRaises(ValueError):
                inspect_archive(path, 'v0.0.2')


if __name__ == '__main__':
    unittest.main()
