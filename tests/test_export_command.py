"""Run the packaged editor entry point as a versioned export subprocess."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import cpu_import_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ng-export-command-') as name:
        root = Path(name)
        project = root / 'project files'
        cpu_import_fixture.write(project)
        job = root / 'job.json'
        backend = {'d3d11': 'dx11', 'd3d12': 'dx12'}.get(
            os.environ.get('MDS_GPU_BACKEND'), os.environ.get('MDS_GPU_BACKEND', 'default'))
        spec = dict(version=1, project='project files/pmx.pmm', destination='sequence', format='png',
                    last=2, width=96, height=64, backend=backend,
                    driver=os.environ.get('MDS_GPU_DRIVER', 'hardware'), cancelFile='cancel')

        def call(expected_code, expected_status, **changes):
            job.write_text(json.dumps(dict(spec, **changes)), encoding='utf-8')
            result = subprocess.run([args.program.resolve(), '--export-job', job], cwd=root,
                                    capture_output=True, text=True, encoding='utf-8', timeout=45)
            assert result.returncode == expected_code, (result.returncode, result.stdout, result.stderr)
            outcome = json.loads(result.stdout)
            assert outcome['version'] == 1 and outcome['status'] == expected_status, outcome
            return outcome

        assert call(1, 'failed', version=2)['reason'] == 'export-job-version-unsupported'
        assert call(1, 'failed', project='missing.pmm')['reason']
        (root / 'cancel').touch()
        assert call(3, 'cancelled')['reason'] == 'export-cancelled'
        assert not (root / 'sequence').exists()
        (root / 'cancel').unlink()
        assert call(0, 'complete')['frames'] == 3
        frames = sorted((root / 'sequence').glob('frame_*.png'))
        assert len(frames) == 3 and all(f.read_bytes().startswith(b'\x89PNG\r\n\x1a\n') for f in frames)
        before = frames[0].read_bytes()
        assert call(1, 'failed')['reason'] == 'export-destination-exists'
        assert frames[0].read_bytes() == before and not list(root.glob('.mds-*'))


if __name__ == '__main__':
    main()
