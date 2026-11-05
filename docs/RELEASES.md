# NG release and qualification guide

NG publishes **portable desktop packages only**. It has no MSI, MSIX, EXE
installer, DMG installer, DEB or RPM. Android is the platform exception: it
requires an APK. The old DX9 application's releases belong to the original
MikuDanceStudio repository and no longer publish from NG.

`VERSION` is the single editable product version, now `0.0.1`, with the format
`major.minor.patch`. CMake, application/About/version output, Windows version
resources, macOS bundle metadata, Conan and archive names read that file. The
matching Git tag is `v0.0.1`. NG does not continue the old library's `10.xx`
version sequence; MMD 9.32 remains the behavior reference.

## Release targets

| Target | Output | Qualification requirement |
| --- | --- | --- |
| Windows x64 | `MikuDanceStudio-NG-vVERSION-windows-x64-portable.zip` | Native x64 editor/CPU tests, WARP presentation, extracted package |
| Windows ARM64 | `MikuDanceStudio-NG-vVERSION-windows-arm64-portable.zip` | Native Windows 11 ARM runner, ARM Qt/DXC/runtime, extracted package |
| macOS Apple Silicon | `MikuDanceStudio-NG-vVERSION-macos-arm64-portable.tar.gz` | Metal, optional MoltenVK, normal/2x presentation, extracted app |
| Linux x64 | `MikuDanceStudio-NG-vVERSION-linux-x64-portable.tar.gz` | Vulkan, X11/Wayland, extracted package on Ubuntu 22.04 |
| Android | `MikuDanceStudio-NG-vVERSION-android-universal.apk` | Both ARM64/x86_64 native libraries, signed APK, same APK installed on isolated x86_64 emulator |

**v0.0.1 is held until all five targets pass.** A platform appearing in this
table is a release requirement, not evidence that its implementation or tests
have already passed. Android requires Android 13+ and Vulkan 1.3; its initial
UI targets landscape tablets. Native Android ARM64 compilation is checked;
emulator execution does not claim physical ARM GPU/driver coverage. macOS Intel,
Linux ARM64, iOS and 32-bit Windows are not current NG release targets.

See the [v0.0.1 qualification record](releases/v0.0.1-qualification.md) for
completed candidate checks and remaining release gates.

Android imports a selected document-provider folder into its private workspace,
preserving project-relative paths without requesting broad storage permissions.
Qualification uses a rootable, non-Play emulator only to transport generated
fixtures and collect reports inside that sandbox. The shipping APK remains
non-debuggable and executes as an ordinary application UID; users do not need
root access.

## One publishing entry point

`.github/workflows/release.yml` is the **only** publisher. Push an immutable
version tag, or manually dispatch it with an existing version tag. It:

1. Checks the exact tag/commit, `VERSION`, release notes and persistent Android
   signing secrets before spending time on platform builds.
2. Calls the existing editor qualification workflow for four desktop targets
   and the Android qualification workflow. It does not run duplicate desktop
   build definitions.
3. Requires successful regressions before packaging. Desktop smoke tests
   verify SHA-256 and archive identity, extract the actual archive, relocate it
   again to a path containing spaces, remove SDK search paths, then open PMM,
   switch all 15 languages and render without showing windows. Android installs
   the same APK that will be published and exercises native rendering, window
   recreation, device replacement and background/resume in an isolated emulator.
4. Checks binary headers, version, clean source identity, tag and full commit.
   Missing/duplicate targets and development packages fail the release.
5. Writes `SHA256SUMS` and `release-manifest.json`, then publishes an early
   **GitHub pre-release** from those artifacts. Existing Releases are immutable:
   no automatic overwrite or retagging.

Source and ordinary test builds use read-only repository permissions. Only the
final publisher gets `contents: write`; it uses the built-in GitHub token, never
a PAT. Native D3DX/compiler oracle DLLs are test-only and never ship in NG.
The independent FX9 metadata oracle exists for Windows x86/x64 only; ARM64 runs
the portable frontend gates and does not claim that native oracle result.

## What each workflow does

| Workflow | Responsibility | Trigger / outputs |
| --- | --- | --- |
| Portable editor | Build shared editor, P4/P5 regressions, presentation, portable deployment | Main/PR; focused manual qualification; reused by Release. Test evidence and development archives |
| Android qualification | Build Qt Android ARM64+x86_64, APK, emulator lifecycle/rendering | Focused manual run or reused by Release; evidence and signed release artifact only when tagged |
| Portable rendering | Backend/resource/pass/GPU contract regressions | Main/PR/manual; test evidence, no product Release |
| Portable formats | Formats, encoding and CPU portability | Main/PR/manual; test evidence, no product Release |
| Bullet 2.75 build | Original-source integrity and compiler/architecture physics checks | Main/PR/manual; test evidence, no product Release |
| Release portable editor | Coordinate qualification and publish all five qualified packages | Version tag or explicit existing-tag dispatch |

Do not dispatch the full matrix to inspect a single platform failure. First
reproduce locally; use the editor's `platform`/qualification inputs or the
Android workflow for the demonstrated gap. Documentation-only changes skip
ordinary builds. Compiler caches are partitioned by OS/architecture/toolchain;
Qt and dependency downloads use pinned versions. Never cache CMake build trees
or reuse machine-specific compiler profiles.

Android builds/signs the APK once, then qualifies that artifact in a separate
emulator job. For changes to diagnostic scripts, manually dispatch `android.yml`
with `candidate_run` set to an earlier run ID to reuse its `android-candidate`
APK. Evidence records that APK's original source identity. This development-only
shortcut cannot publish; Release always builds and tests its own tagged APK.

## Prepare a release

Complete the required platform gates, update `VERSION` and
`docs/releases/vVERSION.md`, then commit all production inputs. Review package
scope against [the cross-platform plan](../cross_platform_plan.md). A preview
release does not imply full P5/MME, Ray/C3 or physical mixed-DPI qualification.

For Android, keep a durable signing keystore and configure the repository's
`ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` and
`ANDROID_KEY_PASSWORD` secrets. Keep an offline backup: subsequent APK updates
must use the same key. Development emulator keys are temporary and never used
for published APKs. Do not commit keystores/passwords or print secrets to logs.

The Android release certificate's SHA-256 fingerprint is
`aea46ca1b43d29b0d784a77d20ba42d4a8bbc714f5e48418432eaacc9a81c30a`.
Check it in `apksigner verify --print-certs` output when auditing a downloaded APK;
development qualification packages deliberately use a different, temporary key.

```sh
python tests/test_release_contract.py
python scripts/release.py --tag v0.0.1
git tag -a v0.0.1 -m "MikuDanceStudio-NG v0.0.1"
git push origin v0.0.1
```

After the single tag workflow completes, verify the public Release's five
packages and manifest. If qualification fails, retain logs and fix the problem;
do not call a failed test a pass, replace published binaries, or relax a gate
just to publish. Fixes before publication can use a new candidate tag/version;
published tags and assets never move.

## Local output layout

| Path | Purpose |
| --- | --- |
| `out/build` | Desktop CMake build and test executables; not a distribution |
| `out/android` | Android CMake/multi-ABI/Gradle intermediates; not a distribution |
| `out/dist` | The current complete desktop runtime directory |
| `out/releases` | Portable archives/APK and SHA-256 files |
| `out/evidence` | Local screenshots, logs and qualification reports |
| `out/qt`, `out/dxc*`, `out/editor-fonts` | Reusable, pinned SDK/dependency inputs |

Do not launch an executable from a random historical build directory or copy
only the EXE out of a portable package. See [distribution instructions](DISTRIBUTION.md).
