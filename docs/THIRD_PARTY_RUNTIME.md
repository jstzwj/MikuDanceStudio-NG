# Runtime components

The editor is distributed under the repository's MIT license. Dynamically
linked Qt remains replaceable by a compatible shared build.

* Qt 6.8.3 (qtbase, qtmultimedia and platform dependencies): LGPLv3/GPL alternatives
  and component-specific third-party notices. License texts are in `Qt/`.
  Corresponding source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
  Third-party attribution: https://doc.qt.io/qt-6.8/licenses-used-in-qt.html
* Bullet 2.75: zlib license, `Bullet/LICENSE.txt`; unmodified source is retained
  under this repository's `recipes/bullet275/bullet-src` directory.
* stb_image: MIT/public-domain alternatives, full notice in `stb_image.h`.
* DXC, when Vulkan or Metal is enabled: upstream licenses/notices in `DXC/`.
  Pinned binary source: https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1
  macOS source revision: b106a961d09221b3c5bdb37be45b679257da08b8.
* SPIRV-Cross, native Metal builds: Apache-2.0/MIT alternatives; statically linked
  MSL compiler, full notice in `SPIRV-Cross/LICENSE`.
  Source: https://github.com/KhronosGroup/SPIRV-Cross/tree/a0fba56c34a6700f1724bf9b751da5b488a3775c
* MoltenVK 1.4.2, macOS builds with Vulkan: Apache-2.0; notices in `MoltenVK/`.
  Source: https://github.com/KhronosGroup/MoltenVK/tree/v1.4.2
* Windows includes Microsoft's redistributable compiler runtime. Linux system
  graphics/audio services and GPU drivers remain host dependencies.
* Linux native WAV output uses the dynamically linked PulseAudio client library
  (LGPL-2.1-or-later), also compatible with PipeWire's PulseAudio service.
  Development builds require `libpulse-dev` (API >=13); installed systems need
  `libpulse.so.0` and a running audio service. Source and license:
  https://gitlab.freedesktop.org/pulseaudio/pulseaudio

The official Qt multimedia deployment may also include FFmpeg shared libraries;
their corresponding source and notices are available in the Qt multimedia source
archive for this exact Qt version. No external models, Ray assets or original
MMD executable are included in this distribution.
