"""Run the relocated production package with no SDK search paths or visible UI."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import cpu_import_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    dist = args.dist.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='mds-relocated-package-') as temporary:
        # macOS exposes /var through /private/var. Compare canonical roots on
        # both sides before rejecting a dependency as external to the package.
        root = Path(temporary).resolve()
        package = root / 'package with spaces'
        shutil.copytree(dist, package, symlinks=True)
        fixtures = root / 'fixtures'
        fixtures.mkdir()
        cpu_import_fixture.write(fixtures)
        system = platform.system()
        if system == 'Windows':
            for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
                if not (package / name).is_file():
                    raise RuntimeError(f'Package depends on a preinstalled MSVC runtime: {name}')
        exe = package / ('MikuDanceStudioNG.exe' if system == 'Windows' else
                         'MikuDanceStudioNG.app/Contents/MacOS/MikuDanceStudioNG' if system == 'Darwin'
                         else 'bin/MikuDanceStudioNG')
        env = {}
        for key in ('SystemRoot', 'WINDIR', 'COMSPEC', 'TEMP', 'TMP', 'HOME', 'USERPROFILE',
                    'LANG', 'LC_ALL', 'XDG_RUNTIME_DIR'):
            if key in os.environ:
                env[key] = os.environ[key]
        env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32') if system == 'Windows' else '/usr/bin:/bin'
        env['QT_QPA_PLATFORM'] = 'offscreen'
        env['QT_DEBUG_PLUGINS'] = '1'
        if system == 'Windows':
            env['MDS_GPU_BACKEND'] = 'd3d11'
            env['MDS_GPU_DRIVER'] = 'software'
        elif system == 'Darwin':
            env['MTL_DEBUG_LAYER'] = '1'
        elif system == 'Linux':
            env['MDS_GPU_DRIVER'] = 'software'
        report = root / 'deployment.json'
        output = []
        # Exercise actual process teardown repeatedly: Qt's Apple logging sink
        # previously failed intermittently while unloading plugins at exit.
        for attempt in range(3 if system == 'Darwin' else 1):
            result = subprocess.run([str(exe), '--deployment-check', str(report), str(fixtures / 'v2.pmm')],
                                    cwd=root, env=env, text=True, encoding='utf-8', errors='replace',
                                    capture_output=True, timeout=120,
                                    creationflags=subprocess.CREATE_NO_WINDOW if system == 'Windows' else 0)
            output.append(f'Process {attempt + 1}, exit {result.returncode}\n' + result.stdout + result.stderr)
            if result.returncode:
                break
        diagnostic = args.report or dist.parent / 'distribution-check.json'
        diagnostic.parent.mkdir(parents=True, exist_ok=True)
        diagnostic.with_suffix('.log').write_text('\n'.join(output), encoding='utf-8')
        if result.returncode:
            print(result.stdout, result.stderr)
            # Keep a native stack for packaging-only crashes on isolated Apple
            # runners. The original failure remains fatal even if rerunning it
            # under the debugger succeeds; never turn this into a retry gate.
            if system == 'Darwin' and os.environ.get('GITHUB_ACTIONS') == 'true':
                try:
                    trace = subprocess.run(
                        ['/usr/bin/lldb', '--no-lldbinit', '--batch', '-o', 'run', '-k', 'thread backtrace all',
                         '--', str(exe), '--deployment-check', str(report), str(fixtures / 'v2.pmm')],
                        cwd=root, env=env, capture_output=True, text=True, encoding='utf-8',
                        errors='replace', timeout=60)
                    diagnostic.with_suffix('.lldb.log').write_text(trace.stdout + trace.stderr,
                                                                   encoding='utf-8')
                    print(trace.stdout, trace.stderr)
                except (OSError, subprocess.TimeoutExpired) as error:
                    print(f'Could not collect native crash stack: {error}')
                # A debugger can change timing or runtime enforcement. Preserve
                # the OS report from the original failed process as well.
                for folder in (Path.home() / 'Library/Logs/DiagnosticReports',
                               Path('/Library/Logs/DiagnosticReports')):
                    for crash in folder.glob('MikuDanceStudioNG*'):
                        if crash.is_file():
                            shutil.copy2(crash, diagnostic.parent /
                                         f'{diagnostic.stem}.{crash.name}')
            raise RuntimeError(f'Relocated distribution failed ({result.returncode}); see {diagnostic.with_suffix(".log")}')
        evidence = json.loads(report.read_text(encoding='utf-8'))
        for key in ('plugins', 'translations', 'assets'):
            path = Path(evidence[key]).resolve()
            if not path.is_relative_to(package) or not path.is_dir():
                raise RuntimeError(f'Package used an external {key} directory: {path}')
            evidence[key] = str(path.relative_to(package))
        if evidence['visible'] or evidence['languages'] != ['en', 'ja', 'zh_CN'] or (evidence['width'], evidence['height']) != (96, 64):
            raise RuntimeError(f'Incomplete package check: {evidence}')
        build = json.loads((package / 'build-info.json').read_text(encoding='utf-8'))
        if system == 'Darwin' and build.get('metal'):
            if not evidence.get('nativeMetal') or not (package / 'licenses/SPIRV-Cross/LICENSE').is_file():
                raise RuntimeError('macOS package did not use native Metal with its compiler notices')
        labels = evidence['standardCancelLabels']
        if labels[0] != 'Cancel' or any(not text or text == 'Cancel' for text in labels[1:]):
            raise RuntimeError(f'Qt standard dialog translations were not deployed: {labels}')
        diagnostic.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
        shutil.copy2(str(report) + '.png', diagnostic.with_suffix('.png'))
        print(result.stdout.strip())


if __name__ == '__main__':
    main()
