"""Collect application symbols and associate them with the packaged binaries."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from release_contract import EXECUTABLES, digest, inspect_archive


def command(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


def build_id(reader, binary):
    match = re.search(r'Build ID:\s*([0-9a-f]+)', command(reader, '-n', binary))
    if not match:
        raise ValueError(f'Missing ELF build ID: {binary.name}')
    return match[1]


def record_symbols(folder, info, **binaries):
    entries = {p.relative_to(folder).as_posix(): digest(p)
               for p in sorted(folder.rglob('*')) if p.is_file() and p.name != 'symbols.json'}
    record = dict(commit=info['commit'], arch=info['arch'], files=entries, **binaries)
    (folder / 'symbols.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


def desktop_symbols(build, stage, metadata):
    system = metadata['system']
    folder = build / 'release-symbols'
    folder.mkdir(exist_ok=True)
    binary = stage / EXECUTABLES[system]
    identity = {}
    if system == 'Windows':
        pdb = Path((build / 'editor-pdb-Release.txt').read_text(encoding='utf-8').strip())
        shutil.copyfile(pdb, folder / 'MikuDanceStudioNG.pdb')
    elif system == 'Darwin':
        dsym = folder / 'MikuDanceStudioNG.dSYM'
        subprocess.run(['dsymutil', str(binary), '-o', str(dsym)], check=True)
        def uuids(path):
            return re.findall(r'UUID:\s*([0-9A-Fa-f-]+)', command('dwarfdump', '--uuid', path))
        if not uuids(binary) or uuids(binary) != uuids(dsym):
            raise ValueError('Packaged Mach-O and dSYM UUID disagree')
        identity['uuid'] = uuids(binary)[0]
    else:
        debug = folder / 'MikuDanceStudioNG.debug'
        subprocess.run(['objcopy', '--only-keep-debug', str(binary), str(debug)], check=True)
        subprocess.run(['objcopy', '--strip-debug', str(binary)], check=True)
        identity['buildId'] = build_id('readelf', binary)
        if build_id('readelf', debug) != identity['buildId']:
            raise ValueError('Packaged ELF and debug file build IDs disagree')
    record_symbols(folder, metadata, binarySha256=digest(binary), **identity)


def android_symbols(build, apk, reader, objcopy):
    info = inspect_archive(apk)
    folder = build / 'release-symbols'
    folder.mkdir(exist_ok=True)
    identities = {}
    with zipfile.ZipFile(apk) as package, tempfile.TemporaryDirectory() as temp:
        for abi in info['abis']:
            name = f'libMikuDanceStudioNG_{abi}.so'
            payload = Path(temp) / name
            payload.write_bytes(package.read(f'lib/{abi}/{name}'))
            identity = build_id(reader, payload)
            candidates = (path for path in build.rglob(name) if 'android-build' not in path.parts)
            native = next((path for path in candidates if build_id(reader, path) == identity
                           and '.debug_line' in command(reader, '-S', path)), None)
            if native is None:
                raise ValueError(f'No symbol-bearing build matches Android {abi} ({identity})')
            debug = folder / f'{abi}.debug'
            subprocess.run([str(objcopy), '--only-keep-debug', str(native), str(debug)], check=True)
            if build_id(reader, debug) != identity:
                raise ValueError(f'Android symbol ID mismatch: {abi}')
            identities[abi] = {'buildId': identity, 'binarySha256': digest(payload)}
    record_symbols(folder, info, packageSha256=info['sha256'], binaries=identities)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--readelf', type=Path, required=True)
    parser.add_argument('--objcopy', type=Path, required=True)
    args = parser.parse_args()
    android_symbols(args.build, args.apk, args.readelf, args.objcopy)
