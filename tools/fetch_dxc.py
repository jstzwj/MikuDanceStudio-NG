"""Fetch checksum-pinned official DXC releases. macOS builds pinned upstream source in CI."""
import argparse
import hashlib
import io
import http.client
from pathlib import Path
import platform
import tarfile
import time
import urllib.error
import urllib.request
import zipfile
from release_contract import binary_arch

VERSION = "v1.8.2505.1"
RELEASES = {
    "Windows": ("dxc_2025_07_14.zip", "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4"),
    "Linux": ("linux_dxc_2025_07_14.x86_64.tar.gz", "f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c"),
}
ASSETS = {"Windows": 272848549, "Linux": 272848547}


def download(name, checksum, asset_id):
    # The release page's CDN can return a cached 504 while the pinned public
    # asset API still works. Keep retries bounded and verify either transport.
    urls = [f"https://github.com/microsoft/DirectXShaderCompiler/releases/download/{VERSION}/{name}"]
    urls += [f"https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/assets/{asset_id}"] * 2
    last_error = None
    for attempt, url in enumerate(urls):
        if attempt:
            time.sleep(attempt * 2)
        request = urllib.request.Request(url, headers={
            "User-Agent": "MikuDanceStudio-NG", "Accept": "application/octet-stream"})
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                data = response.read()
        except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as error:
            last_error = error
            print(f"DXC download attempt {attempt + 1}/{len(urls)} failed: {type(error).__name__}", flush=True)
            continue
        if hashlib.sha256(data).hexdigest() != checksum:
            raise RuntimeError("DXC archive checksum mismatch")
        return data
    raise RuntimeError("DXC download failed after three transport attempts") from last_error

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arch", choices=('x64', 'arm64'),
                        default='arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64')
    args = parser.parse_args()
    system = platform.system()
    name, checksum = RELEASES[system]
    data = download(name, checksum, ASSETS[system])
    args.output.mkdir(parents=True, exist_ok=True)
    # Extract an explicit allowlist, preserving the compiler's bin/../lib rpath.
    if system == "Windows":
        archive = zipfile.ZipFile(io.BytesIO(data))
        for item in ("dxc.exe", "dxcompiler.dll", "dxil.dll"):
            data = archive.read(f"bin/{args.arch}/{item}")
            if binary_arch(data) != args.arch:
                raise RuntimeError(f'DXC distribution has the wrong architecture: {item}')
            (args.output / item).write_bytes(data)
        for item in ("LICENSE-LLVM.txt", "LICENSE-MS.txt", "ReleaseNotes.md"):
            if item in archive.namelist(): (args.output / item).write_bytes(archive.read(item))
    else:
        if args.arch != 'x64':
            raise RuntimeError('The pinned Linux DXC archive supports x64 only')
        archive = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
        for item in ("bin/dxc", "lib/libdxcompiler.so", "lib/libdxil.so", "LICENSE-LLVM.txt", "LICENSE-MS.txt"):
            member = next((m for m in archive if m.name.lstrip("./") == item), None)
            if member is None or not member.isfile(): raise RuntimeError(f"missing regular file: {item}")
            target = args.output / item
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())
            target.chmod(0o755 if item.startswith(("bin/", "lib/")) else 0o644)
    print(f"DXC {VERSION}: SHA256 verified")

if __name__ == "__main__": main()
