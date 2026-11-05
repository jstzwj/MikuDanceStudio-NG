"""Install the actual APK on an isolated emulator; check GPU, language and lifecycle."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
import shlex
import re
import cpu_import_fixture

PACKAGE = 'io.github.jstzwj.mikudancestudiong'
ACTIVITY = PACKAGE + '/org.qtproject.qt.android.bindings.QtActivity'
FILES = '/data/user/0/' + PACKAGE + '/files'


def adb(*args, **kwargs):
    return subprocess.run(['adb', *args], check=True, **kwargs)


def read_report(name):
    result = subprocess.run(['adb', 'shell', 'cat', FILES + '/' + name], capture_output=True, text=True)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return None


def wait_report(name, expected):
    deadline = time.monotonic() + 150
    stopped = 0
    while time.monotonic() < deadline:
        report = read_report(name)
        if report and report.get('state') == 'failed':
            raise RuntimeError(f'Android gate failed: {report}')
        if report and (not expected or report.get('state') in expected):
            return report
        process = subprocess.run(['adb', 'shell', 'pidof', PACKAGE], capture_output=True, text=True)
        stopped = 0 if process.stdout.strip() else stopped + 1
        if stopped >= 10:
            raise RuntimeError(f'Android process exited before writing {name}; see logcat.txt')
        time.sleep(1)
    raise RuntimeError(f'Android gate timed out waiting for {name}: {read_report(name)}')


def start(arguments, driver):
    # QtActivity's loader accepts this documented development extra. No shell
    # interpolation or real-device input is used; this runner owns the emulator.
    arguments = '--gpu-driver ' + driver + ' ' + arguments
    adb('shell', 'am', 'start', '-W', '-n', ACTIVITY, '--es', 'applicationArguments', shlex.quote(arguments))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/android-tests'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    emulator = adb('shell', 'getprop', 'ro.kernel.qemu', capture_output=True, text=True)
    if emulator.stdout.strip() != '1':
        raise RuntimeError('Android qualification requires an isolated emulator, not a physical device')
    # A release APK stays non-debuggable and runs as its normal application UID.
    # Root is only an emulator-side fixture/report transport into that sandbox;
    # no external-storage permission or production-device root is required.
    adb('root')
    adb('wait-for-device')
    if adb('shell', 'id', '-u', capture_output=True, text=True).stdout.strip() != '0':
        raise RuntimeError('Use an isolated, rootable non-Play emulator image for qualification')
    try:
        adb('install', '-r', str(args.apk))
        packages = adb('shell', 'cmd', 'package', 'list', 'packages', '-U', PACKAGE,
                       capture_output=True, text=True).stdout
        application_uid = re.search(r'package:' + re.escape(PACKAGE) + r'\s+uid:(\d+)', packages)
        if not application_uid:
            raise RuntimeError('Cannot determine installed application UID')
        uid = application_uid.group(1)
        adb('shell', 'mkdir', '-p', FILES + '/fixtures')
        with tempfile.TemporaryDirectory() as temporary:
            cpu_import_fixture.write(Path(temporary))
            adb('push', *(str(path) for path in sorted(Path(temporary).iterdir())), FILES + '/fixtures/')
        adb('shell', 'chown', '-R', uid + ':' + uid, FILES)
        adb('shell', 'restorecon', '-RF', FILES)
        adb('shell', 'test', '-r', FILES + '/fixtures/v2.pmm')
        # Query the guest device, not the runner host. SwiftShader is a CPU
        # device and must use the same explicit software selection as desktops.
        gpu = adb('shell', 'cmd', 'gpu', 'vkjson', capture_output=True, text=True).stdout
        (args.output / 'vulkan-device.json').write_text(gpu, encoding='utf-8')
        devices = json.loads(gpu)['devices']
        driver = 'software' if devices and all(d['properties']['deviceType'] == 4 for d in devices) else 'hardware'
        print('Android guest Vulkan:', [(d['properties']['deviceName'], d['properties']['apiVersion'],
                                         d['properties']['deviceType']) for d in devices], 'driver:', driver)
        adb('logcat', '-c')
        start('--deployment-check ' + FILES + '/deployment.json ' + FILES + '/fixtures/v2.pmm', driver)
        deployment = wait_report('deployment.json', None)
        version = (Path(__file__).resolve().parents[1] / 'VERSION').read_text().strip()
        if deployment.get('version') != version or len(deployment.get('languages', [])) != 15:
            raise RuntimeError(f'Incomplete Android deployment check: {deployment}')
        if deployment.get('width') != 96 or deployment.get('height') != 64 or not deployment.get('vulkan'):
            raise RuntimeError(f'Android offscreen rendering did not pass: {deployment}')
        (args.output / 'deployment.json').write_text(json.dumps(deployment, indent=2) + '\n')
        adb('pull', FILES + '/deployment.json.png', str(args.output / 'offscreen.png'))
        adb('shell', 'am', 'force-stop', PACKAGE)
        arguments = '--android-acceptance ' + FILES + '/native.json ' + FILES + '/fixtures/v2.pmm'
        start(arguments, driver)
        wait_report('native.json', ('await-lifecycle',))
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        time.sleep(2)
        start(arguments, driver)
        native = wait_report('native.json', ('passed',))
        if not native.get('suspended') or not native.get('resumed') or native.get('submissions', 0) < 3:
            raise RuntimeError(f'Android native lifecycle was not exercised: {native}')
        (args.output / 'native.json').write_text(json.dumps(native, indent=2) + '\n')
        with (args.output / 'presentation.png').open('wb') as image:
            adb('exec-out', 'screencap', '-p', stdout=image)
    finally:
        adb('shell', 'am', 'force-stop', PACKAGE)
        log = adb('logcat', '-d', capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
        (args.output / 'logcat.txt').write_text(log, encoding='utf-8')
        markers = ('Qt', ' default :', 'AndroidRuntime', 'MikuDance', 'mikudancestudio',
                   'vulkan', 'FATAL', 'Fatal', 'Abort message', 'SIGSEGV', 'SIGABRT')
        print('\n'.join([line for line in log.splitlines() if any(marker in line for marker in markers)][-100:]))


if __name__ == '__main__':
    main()
