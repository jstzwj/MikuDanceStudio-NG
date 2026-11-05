# MikuDanceStudio NG

**English** · [简体中文](README.zh-CN.md) · [日本語](README.ja.md)

**A new chapter for MikuMikuDance.**

MikuDanceStudio NG carries the familiar MMD animation workflow toward modern
rendering and a native editor for Windows, macOS and Linux. It grows from a
source-level reconstruction of MikuMikuDance v9.32, with the original program's
behavior as our reference.

Release policy: **portable desktop packages only** (no installer), plus an
Android APK. The first `v0.0.1` preview is being prepared for Windows x64/ARM64,
Apple Silicon macOS, Linux x64 and Android; publication waits for all five
targets to pass. See the [release and testing guide](docs/RELEASES.md).

## Our story

Eight years ago, I spent a night talking with **Rui, the author of
[Ray-MMD](https://github.com/ray-cast/ray-mmd)**. We talked until morning about
why Ray-MMD could not keep moving forward, and the limits of MMD's DirectX 9
renderer. There was still so much we wanted to do with it.

Rui also tried building a rendering engine of his own. He spent five years
on that path. Working within the constraints of MMD's ecosystem, the attempt
ultimately fell short of the result and the experience he had hoped for.

That history matters to this project. A renderer lives alongside models,
motions, effects, years of accumulated work, and the small habits that make a
tool feel like home. Bringing all of that forward takes more than a new way to
draw a frame.

Over the past two years, the rapid progress of AI gave us a new opening. It
helped us look inside MMD, study its mechanisms, and reconstruct its
implementation as source code. We finally had a foundation for the whole
application that we could read, change, and continue developing.

That is where **MikuDanceStudio NG** begins.

We want to build a better MMD together: preserve the care in its workflow, make
room for modern graphics, and let more people create on the computers they
already use. The models, motions, effects and scenes people have made over the
years deserve a future, too.

The conversation from eight years ago is still with us. Now we can keep working
on the questions it left behind.

## What we are building

- **A familiar place to animate.** Bones, morphs, IK, physics, camera, lighting,
  accessories, the timeline and undo/redo, guided by original MMD behavior.
- **Continuity for existing work.** PMD/PMX models, VMD motions, VPD poses and PMM
  projects, with compatibility fixes checked against real scenes and generated
  regression fixtures.
- **Modern rendering foundations.** A shared RHI for DirectX 11, DirectX 12,
  Vulkan and native Metal, including materials, self-shadowing, multiple render
  targets, HDR intermediates and post-processing.
- **An editor in more languages.** Qt on all three platforms, with 15 UI
  languages selected through **File → Preferences… → Language**.
- **A path for MME and Ray-MMD.** Shared effect descriptions, resources and pass
  scheduling, with compatibility expanded and verified effect by effect.

## Where we are

NG is under active development. The portable core, CPU scene runtime and initial
cross-platform rendering milestones **P0–P3 are complete**. The **P4 editor milestone
is accepted under a revised scope**, with physical mixed-DPI monitor testing
deferred as a known limitation. Next is [P5: cross-platform MME](docs/CROSS_PLATFORM_P5.md).
Full MME and Ray-MMD compatibility remains ongoing work; current
support covers the documented effect subsets.

| Platform | NG rendering backends | Current development baseline |
| --- | --- | --- |
| Windows | DirectX 11, DirectX 12, Vulkan | Windows 10/11, x64 |
| macOS | Native Metal; optional Vulkan through MoltenVK | macOS 13+, Apple Silicon |
| Linux | Vulkan, with X11 and Wayland | Ubuntu 22.04, x86_64 |

The [acceptance status](docs/P4_ACCEPTANCE_STATUS.md),
[porting plan](cross_platform_plan.md) and
[RHI capability table](docs/RHI_FEATURE_SUPPORT.md) describe what has actually
been tested and what remains. Development packages are available from the
[Portable editor workflow](https://github.com/jstzwj/MikuDanceStudio-NG/actions/workflows/portable-editor.yml).

This repository preserves the full history of the former `next` branch of
[MikuDanceStudio](https://github.com/jstzwj/MikuDanceStudio). That repository
continues to maintain the Windows compatibility branch; NG develops independently
on `main`.

## Building from source

Use a C++17 compiler, CMake 3.21 or newer, Python 3, and Qt 6.8.3 with Widgets,
Multimedia and Linguist tools. The rendering backends also need their platform
SDKs and shader tools; see the [platform setup and packaging guide](docs/DISTRIBUTION.md)
for DXC, Vulkan, Metal and Linux system dependencies.

```sh
git clone https://github.com/jstzwj/MikuDanceStudio-NG.git
cd MikuDanceStudio-NG
cmake -S cmake/portable-editor -B out/build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build out/build --config Release --target dist --parallel 3
```

`out/dist` contains the application and its runtime dependencies. On Windows,
run `out/dist/MikuDanceStudioNG.exe`; on macOS, open the `.app`; on Linux, run
`out/dist/bin/MikuDanceStudioNG`. Keep the complete package when moving it to
another computer. Build files stay in `out/build`, and archives in `out/releases`.

## Help write the next chapter

Bring a scene that behaves differently, an effect that fails to load, a clearer
translation, or a patch. Small, reproducible examples help us preserve the
details that matter to people who use MMD every day. Please include the platform,
rendering backend and steps to reproduce when reporting an issue.

[Report an issue](https://github.com/jstzwj/MikuDanceStudio-NG/issues) ·
[Documentation](docs/README.md) · [Translation guide](translations/README.md) ·
[Release guide](docs/RELEASES.md) · [Version](VERSION)

## With thanks

To **Yu Higuchi (樋口優)**, for creating
[MikuMikuDance](https://learnmmd.com/downloads/) and giving so many people a place
to begin creating. To **Rui**, for Ray-MMD, the years spent exploring what
could come next, and that conversation eight years ago.

And to the authors of models, motions, effects, music and tutorials, to the MME
community, and to everyone who has shared what they learned. Their work is a
large part of what makes MMD worth carrying forward. We also thank the
maintainers of Qt, Bullet Physics and the open-source tools this project uses.

## License and origins

MikuDanceStudio's own code is available under the [MIT License](LICENSE).
Original MMD resources retain their original terms; see the preserved
[permission statement](licenses/MikuMikuDance.txt) and
[resource provenance](res/README.md). Third-party components retain their
respective licenses.
