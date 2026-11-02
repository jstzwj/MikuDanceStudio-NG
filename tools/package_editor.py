"""Build a relocatable Qt editor dist through CMake's Runtime install component."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def copy_file(source, target):
    if not source.is_file():
        raise RuntimeError(f"Missing distribution dependency: {source}")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def relocate_macho(path, runpath, libraries):
    # Remove build-tree search paths, so a relocation test cannot accidentally
    # pass by loading the compiler's original build instead of the shipped copy.
    commands = run('otool', '-l', path, capture_output=True, text=True).stdout
    paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands)
    for old in paths:
        if old != runpath:
            run('install_name_tool', '-delete_rpath', old, path)
    if runpath not in paths:
        run('install_name_tool', '-add_rpath', runpath, path)
    links = run('otool', '-L', path, capture_output=True, text=True).stdout
    for line in links.splitlines()[1:]:
        old = line.strip().split(' (compatibility version')[0]
        if Path(old).name in libraries and old != '@rpath/' + Path(old).name:
            run('install_name_tool', '-change', old, '@rpath/' + Path(old).name, path)
    if path.suffix == '.dylib':
        run('install_name_tool', '-id', '@rpath/' + path.name, path)


def runtime(stage, config):
    system = config['system']
    if system == 'Darwin':
        bundle = stage / 'MikuDanceStudioNG.app'
        bindir = bundle / 'Contents/MacOS'
        libdir = bundle / 'Contents/Frameworks'
        plugins = bundle / 'Contents/PlugIns'
        translations = bundle / 'Contents/Resources/translations'
    else:
        bindir = stage if system == 'Windows' else stage / 'bin'
        libdir = stage if system == 'Windows' else stage / 'lib'
        plugins, translations = stage / 'plugins', stage / 'translations'
    qt = {}
    for name in ('QT_INSTALL_PLUGINS', 'QT_INSTALL_TRANSLATIONS'):
        qt[name] = Path(run(config['qmake'], '-query', name, capture_output=True,
                            text=True).stdout.strip())
    # The shipping platform plugin comes from Qt's deploy API. Include offscreen
    # explicitly as well, so the relocated app can validate itself without UI.
    extension = '.dll' if system == 'Windows' else '.dylib' if system == 'Darwin' else '.so'
    prefix = '' if system == 'Windows' else 'lib'
    offscreen = plugins / 'platforms' / f'{prefix}qoffscreen{extension}'
    copy_file(qt['QT_INSTALL_PLUGINS'] / 'platforms' / offscreen.name, offscreen)
    if system == 'Linux':
        run('patchelf', '--set-rpath', '$ORIGIN/../../lib', offscreen)
    for code in ('ja', 'zh_CN'):
        copy_file(qt['QT_INSTALL_TRANSLATIONS'] / f'qtbase_{code}.qm',
                  translations / f'qtbase_{code}.qm')
    if system == 'Windows':
        for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
            if not (bindir / name).is_file():
                raise RuntimeError(f'The configured MSVC redistributables are missing {name}')
    if config['vulkan']:
        compiler = Path(config['dxc'])
        runtime_dir = Path(config['dxc_runtime']) if config['dxc_runtime'] else compiler.parent
        copy_file(compiler, bindir / compiler.name)
        if system == 'Windows':
            for filename in ('dxcompiler.dll', 'dxil.dll'):
                copy_file(runtime_dir / filename, bindir / filename)
        else:
            search = runtime_dir / 'lib' if (runtime_dir / 'lib').is_dir() else runtime_dir / '../lib'
            libraries = sorted(search.glob('libdx*.dylib' if system == 'Darwin' else 'libdx*.so*'))
            if not libraries:
                raise RuntimeError(f'DXC runtime libraries missing in {search}')
            for library in libraries:
                copy_file(library, libdir / library.name)
            if system == 'Darwin':
                names = {library.name for library in libraries}
                relocate_macho(bindir / compiler.name, '@executable_path/../Frameworks', names)
                for library in libraries:
                    relocate_macho(libdir / library.name, '@loader_path', names)
            else:
                run('patchelf', '--set-rpath', '$ORIGIN/../lib', bindir / compiler.name)
                for library in libraries:
                    run('patchelf', '--set-rpath', '$ORIGIN', libdir / library.name)
        notices = set()
        for folder in (runtime_dir, compiler.parent, compiler.parent.parent):
            for pattern in ('LICENSE*', 'ThirdPartyNotices*'):
                for path in folder.glob(pattern):
                    if path.is_file() and path.name not in notices:
                        copy_file(path, stage / 'licenses/DXC' / path.name)
                        notices.add(path.name)
        if not notices:
            raise RuntimeError('Use a DXC runtime distribution containing its license files')
    if system == 'Darwin':
        molten = Path(config['moltenvk'])
        copy_file(molten, libdir / 'libMoltenVK.dylib')
        notices = list(molten.parent.glob('LICENSE*'))
        if not notices:
            raise RuntimeError('MoltenVK runtime is missing its license notices')
        for path in notices:
            copy_file(path, stage / 'licenses/MoltenVK' / path.name)
        # macdeployqt has already fixed framework dependencies. Fix the extra
        # offscreen plugin too; it was copied after that deployment step.
        tool = Path(config['qmake']).parent / 'macdeployqt'
        run(tool, bundle, '-always-overwrite', '-no-strip')
        run('codesign', '--force', '--deep', '--sign', '-', bundle)
    return bindir


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    if args.config != 'Release':
        raise RuntimeError('Distribution builds must use Release')
    config = json.loads((build / 'qt-dist-config.json').read_text(encoding='utf-8'))
    source = Path(config['source'])
    dist = build / 'dist'
    # Only replace our own explicitly marked output; never delete an arbitrary
    # user directory or a symlink that happens to be named dist.
    if dist.is_symlink() or (dist.exists() and not (dist / '.mds-dist').is_file()):
        raise RuntimeError(f'Refusing to replace an unowned distribution directory: {dist}')
    with tempfile.TemporaryDirectory(prefix='mds-dist-', dir=build) as temporary:
        stage = Path(temporary) / 'dist'
        run(args.cmake, '--install', build, '--config', args.config,
            '--component', 'Runtime', '--prefix', stage)
        runtime(stage, config)
        copy_file(source / 'docs/THIRD_PARTY_RUNTIME.md', stage / 'licenses/README.md')
        copy_file(source / 'licenses/MikuMikuDance.txt', stage / 'licenses/MikuMikuDance.txt')
        shutil.copytree(source / 'res/licenses/Qt', stage / 'licenses/Qt')
        # stb carries its public-domain/MIT notice in the source distribution.
        copy_file(source / 'third_party/stb/stb_image.h', stage / 'licenses/stb_image.h')
        commit = run('git', '-C', source, 'rev-parse', 'HEAD', capture_output=True, text=True).stdout.strip()
        metadata = {key: config[key] for key in ('system', 'qt', 'version', 'vulkan')}
        dirty = bool(run('git', '-C', source, 'status', '--porcelain', '--untracked-files=no',
                         capture_output=True, text=True).stdout.strip())
        metadata.update(commit=commit, arch=platform.machine(), development=True, modified=dirty)
        (stage / 'build-info.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        (stage / '.mds-dist').write_text('MikuDanceStudio-NG distribution\n', encoding='utf-8')
        # Publish only a complete installation; a failed deploy retains the old dist.
        if dist.exists():
            assert dist.resolve().parent == build
            shutil.rmtree(dist)
        shutil.move(stage, dist)
    suffix = {'Windows': 'windows', 'Linux': 'linux', 'Darwin': 'macos'}[config['system']]
    arch = {'AMD64': 'x64', 'x86_64': 'x64', 'aarch64': 'arm64'}.get(platform.machine(), platform.machine())
    name = f'MikuDanceStudio-NG-{config["version"]}-{suffix}-{arch}'
    archive = build / (name + ('.zip' if suffix == 'windows' else '.tar.gz'))
    if suffix == 'windows':
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as target:
            for path in sorted(dist.rglob('*')):
                if path.is_file():
                    target.write(path, Path(name) / path.relative_to(dist))
    else:
        with tarfile.open(archive, 'w:gz') as target:
            target.add(dist, arcname=name)
    with archive.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
    print(f'Distribution: {dist}\nArchive: {archive}\nSHA256: {checksum}')


if __name__ == '__main__':
    main()
