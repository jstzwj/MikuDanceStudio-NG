"""Check original FX loading and stable public metadata without a UI or device."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--dxc', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    program, dxc = str(args.program.resolve()), str(args.dxc.resolve())
    fixture = args.source / 'assets/effects/frontend/Inspect.fx'

    def inspect(path, *extra, status=0):
        result = subprocess.run([program, 'inspect', str(path.resolve()), '--dxc', dxc,
                                 *extra], capture_output=True, encoding='utf-8', timeout=45)
        assert result.returncode == status, (result.stdout, result.stderr)
        assert not result.stderr, result.stderr
        return json.loads(result.stdout)

    report = inspect(fixture, '-D', 'MME_MIPMAP=')
    assert report == inspect(fixture, '-D', 'MME_MIPMAP=')
    assert report['status'] == 'described' and not report['executionValidated']
    assert report['scene'] and report['macros'] == {'MME_MIPMAP': '', '_INDEX': 'PSIZE15'}
    assert [f['path'] for f in report['files']] == ['Inspect.fx', 'parameters.fxh']
    assert report['includes'] == [{'parent': 0, 'child': 1, 'line': 2}]
    variables = {v['name']: v for v in report['parameters']}
    assert variables['Tint']['shared'] and variables['Tint']['defaultExpression'] == '{ 0.25 , 0.5 , 0.75 , 1 } '
    assert variables['WorldViewProjection']['rows'] == 4
    assert variables['Weights']['elements'] == 2
    assert variables['Tag']['semantic'] == 'PSIZE15'
    assert '日本語 / 中文' in variables['Caption']['defaultExpression']
    technique = report['techniques'][0]
    assert [p['name'] for p in technique['passes']] == ['Capture', 'Final']
    assert technique['annotations']['SCRIPT'] == 'RenderColorTarget0=ColorTarget;Pass=Capture;ScriptExternal=Color;'
    assert technique['passes'][0]['annotations']['SCRIPT'] == 'Draw=Buffer;'
    assert report['textures'][0]['annotations']['VIEWPORTRATIO'] == '{0.5,0.5}'
    assert report['samplers'][0]['clampU'] and not report['samplers'][0]['linearMip']
    baseline = inspect(fixture)
    assert baseline['parameters'][0]['defaultExpression'] == '{ 1 , 1 , 1 , 1 } '
    # The exact same description must survive a Unicode path and spaces.
    relocated = args.output / '日本語 中文 space'
    shutil.copytree(fixture.parent, relocated, dirs_exist_ok=True)
    assert report == inspect(relocated / fixture.name, '-D', 'MME_MIPMAP=')
    (relocated / 'parameters.fxh').write_text('#include "missing.fxh"\n', encoding='utf-8')
    failed = inspect(relocated / fixture.name, status=1)
    assert failed['status'] == 'failed' and not failed['executionValidated']
    assert failed['diagnostic']['stage'] == 'preprocess'
    assert failed['diagnostic']['line'] == 1 and 'parameters.fxh' in failed['diagnostic']['file']
    invalid = args.output / 'invalid.fx'
    invalid.write_text('technique T { pass P { Bad 0; }}', encoding='utf-8')
    failed = inspect(invalid, status=1)
    assert failed['diagnostic']['stage'] == 'source' and failed['diagnostic']['technique'] == 'T'
    assert failed['diagnostic']['pass'] == 'P'
    for name in ['LegacyMaterial.fx', 'LegacyScene.fx']:
        original = inspect(args.source / 'assets/effects/portable' / name)
        assert original['techniques']
    destination = args.output / 'report.json'
    result = subprocess.run([program, 'inspect', str(fixture.resolve()), '--dxc', dxc,
                             '-D', 'MME_MIPMAP=', '--output', str(destination.resolve())],
                            capture_output=True, timeout=45)
    assert result.returncode == 0 and not result.stdout and not result.stderr
    assert json.loads(destination.read_text(encoding='utf-8')) == report
    print('FX CLI: stable metadata, native defaults/scripts, Unicode relocation, diagnostics and file output passed')


if __name__ == '__main__':
    main()
