"""Fetch the pinned Microsoft D3DX runtime for independent Windows tests only."""
import argparse
import hashlib
import io
from pathlib import Path
import urllib.request
import zipfile

VERSION = '9.29.952.8'
SHA256 = 'ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b'
URL = f'https://api.nuget.org/v3-flatcontainer/microsoft.dxsdk.d3dx/{VERSION}/microsoft.dxsdk.d3dx.{VERSION}.nupkg'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = urllib.request.urlopen(URL, timeout=120).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError('D3DX oracle package checksum mismatch')
    archive = zipfile.ZipFile(io.BytesIO(data))
    args.output.mkdir(parents=True, exist_ok=True)
    # Deliberately do not install, register or distribute the legacy runtime
    # with the Qt editor. It is an independent test dependency only.
    (args.output / 'D3DX9_43.dll').write_bytes(archive.read('build/native/release/bin/x64/D3DX9_43.dll'))
    for item in archive.namelist():
        if 'license' in item.lower() and item.lower().endswith(('.txt', '.rtf')):
            (args.output / Path(item).name).write_bytes(archive.read(item))
    print(f'D3DX oracle {VERSION}: SHA256 verified (test only)')


if __name__ == '__main__':
    main()
