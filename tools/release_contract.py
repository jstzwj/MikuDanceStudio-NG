"""Shared version, binary identity and archive checks for NG portable releases."""
import hashlib
import json
from pathlib import PurePosixPath
import re
import struct
import tarfile
import zipfile

PRODUCT = 'MikuDanceStudio-NG'
RELEASE_TARGETS = ('windows-x64', 'windows-arm64', 'macos-arm64', 'linux-x64', 'android-universal')
SYSTEMS = {'Windows': 'windows', 'Darwin': 'macos', 'Linux': 'linux', 'Android': 'android'}
EXECUTABLES = {'Windows': 'MikuDanceStudioNG.exe',
               'Linux': 'bin/MikuDanceStudioNG',
               'Darwin': 'MikuDanceStudioNG.app/Contents/MacOS/MikuDanceStudioNG'}


def version(value):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', value):
        raise ValueError('VERSION must be major.minor.patch without leading zeros')
    if any(int(part) > 65535 for part in value.split('.')):
        raise ValueError('VERSION exceeds the Windows version-resource limit')
    return value


def binary_arch(data):
    """Read the executable, never the packaging host's architecture."""
    if data[:2] == b'MZ' and len(data) >= 64:
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset + 4] != b'PE\0\0':
            raise ValueError('Invalid PE header')
        machine = struct.unpack_from('<H', data, offset + 4)[0]
        architectures = {0x8664: 'x64', 0xaa64: 'arm64'}
    elif data[:4] == b'\x7fELF' and len(data) >= 20 and data[4:6] == b'\x02\x01':
        machine = struct.unpack_from('<H', data, 18)[0]
        architectures = {62: 'x64', 183: 'arm64'}
    elif data[:4] == b'\xcf\xfa\xed\xfe' and len(data) >= 8:
        machine = struct.unpack_from('<I', data, 4)[0]
        architectures = {0x1000007: 'x64', 0x100000c: 'arm64'}
    else:
        raise ValueError('Unsupported executable format (expected 64-bit PE, ELF or Mach-O)')
    if machine not in architectures:
        raise ValueError(f'Unsupported executable architecture: {machine:#x}')
    return architectures[machine]


def stem(info):
    return f'{PRODUCT}-v{version(info["version"])}-{SYSTEMS[info["system"]]}-{info["arch"]}-portable'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inspect_archive(path, tag=None, commit=None):
    """Check the published payload and its checksum without executing it."""
    checksum = digest(path)
    if path.with_name(path.name + '.sha256').read_text(encoding='ascii').strip() != f'{checksum}  {path.name}':
        raise ValueError(f'Checksum mismatch: {path.name}')
    if path.name.endswith('.apk'):
        with zipfile.ZipFile(path) as archive:
            info = json.loads(archive.read('assets/build-info.json'))
            if info.get('schema') != 1 or info.get('product') != PRODUCT or info.get('format') != 'apk':
                raise ValueError('Missing Android build identity')
            expected = f'{PRODUCT}-v{version(info["version"])}-android-{info["arch"]}.apk'
            if path.name != expected or info['system'] != 'Android':
                raise ValueError('APK filename does not describe its payload')
            expected_abis = {'universal': ['arm64-v8a', 'x86_64'],
                             'arm64': ['arm64-v8a'], 'x64': ['x86_64']}[info['arch']]
            if sorted(info.get('abis', [])) != sorted(expected_abis):
                raise ValueError('APK ABI list does not describe its payload')
            for abi in expected_abis:
                native = f'lib/{abi}/libMikuDanceStudioNG_{abi}.so'
                if binary_arch(archive.read(native)) != ('arm64' if abi == 'arm64-v8a' else 'x64'):
                    raise ValueError('APK native library architecture mismatch')
            if tag and (info.get('tag') != tag or tag != 'v' + info['version'] or
                        info.get('development') is not False or info.get('modified') is not False):
                raise ValueError('APK is not an unmodified tagged release')
            if commit and info.get('commit') != commit:
                raise ValueError('APK source commit mismatch')
        return dict(info, archive=path.name, sha256=checksum, bytes=path.stat().st_size)
    opener = zipfile.ZipFile if path.name.endswith('.zip') else tarfile.open
    with opener(path) as archive:
        zipped = isinstance(archive, zipfile.ZipFile)
        names = archive.namelist() if zipped else archive.getnames()
        roots = set()
        for name in names:
            part = PurePosixPath(name)
            if not name or part.is_absolute() or '..' in part.parts or '\\' in name:
                raise ValueError(f'Unsafe archive path: {name}')
            roots.add(part.parts[0])
        if len(roots) != 1:
            raise ValueError('Portable archive must contain one top-level directory')
        root = roots.pop()

        def read(name):
            full = root + '/' + name
            return archive.read(full) if zipped else archive.extractfile(full).read()

        info = json.loads(read('build-info.json'))
        if info.get('schema') != 1 or info.get('product') != PRODUCT or info.get('format') != 'portable':
            raise ValueError('Missing NG portable build identity')
        expected_stem = stem(info)
        extension = '.zip' if info['system'] == 'Windows' else '.tar.gz'
        if root != expected_stem or path.name != expected_stem + extension:
            raise ValueError(f'Archive filename does not describe its payload: {path.name}')
        if read('VERSION').decode('utf-8').strip() != version(info['version']):
            raise ValueError('Packaged VERSION and build identity disagree')
        if binary_arch(read(EXECUTABLES[info['system']])) != info['arch']:
            raise ValueError('Executable architecture and build identity disagree')
        if not re.fullmatch('[0-9a-f]{40}', info.get('commit', '')):
            raise ValueError('Missing full source commit')
        if tag:
            if tag != 'v' + info['version'] or info.get('tag') != tag:
                raise ValueError('Release tag does not match package version')
            if info.get('development') is not False or info.get('modified') is not False:
                raise ValueError('Development or modified packages cannot be released')
        if commit and info['commit'] != commit:
            raise ValueError('Package was built from a different source commit')
    return dict(info, archive=path.name, sha256=checksum, bytes=path.stat().st_size)


def verify_packages(folder, tag, commit, targets=RELEASE_TARGETS):
    records = [inspect_archive(path, tag, commit) for path in sorted(folder.iterdir())
               if path.name.endswith(('.zip', '.tar.gz', '.apk'))]
    actual = [SYSTEMS[item['system']] + '-' + item['arch'] for item in records]
    if sorted(actual) != sorted(targets):
        raise ValueError(f'Release requires exactly {list(targets)}; found {actual}')
    manifest = dict(schema=1, product=PRODUCT, tag=tag, commit=commit, packages=records)
    (folder / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (folder / 'SHA256SUMS').write_text(''.join(f'{item["sha256"]}  {item["archive"]}\n'
                                            for item in records), encoding='ascii')
    return manifest
