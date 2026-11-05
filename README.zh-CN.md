# MikuDanceStudio NG

[English](README.md) · **简体中文** · [日本語](README.ja.md)

**把 MikuMikuDance 的故事，继续写下去。**

MikuDanceStudio NG 希望把我们熟悉的 MMD 动画工作流，带到现代渲染技术，以及 Windows、macOS 和 Linux 的原生编辑器上。项目从 MikuMikuDance v9.32 的源码级重建出发，以原版程序的行为为对照，一点点向前。

发行方式：**桌面只提供便携包，不做安装版**；Android 提供 APK。当前 `v0.0.2` 预览版面向 Windows x64／ARM64、Apple Silicon macOS、Linux x64 和 Android。每次发布均要求五种目标全部通过，合格包见 [Releases](https://github.com/jstzwj/MikuDanceStudio-NG/releases)。构建、测试、打包和签名职责统一见[发布指南](docs/RELEASES.md)。

## 我们的故事

故事的开始，要回到八年前。

那一晚，我和 **[Ray-MMD](https://github.com/ray-cast/ray-mmd) 的作者 Rui** 聊了很久，一直聊到天亮。我们聊 Ray-MMD 为什么难以继续做下去，聊 MMD 的 DirectX 9 渲染有哪些限制，也聊那些还想实现、却被挡在门外的东西。

Rui 也尝试过另一条路：自己写一个渲染引擎。他为此投入了五年。受限于 MMD 本身的生态，这次尝试最终仍没能走到理想中的结果，使用体验也没有达到他的期待。

这段经历，一直留在这个项目的起点。一个渲染器的身后，还有模型、动作、特效，还有多年积累下来的作品与使用习惯。要把这些一起带向未来，需要照顾的，是创作过程里的每一个细节。

这两年，AI 的快速发展给了我们一个新的契机。借助这些工具，我们得以深入理解 MMD 的内部机制，并将原版的实现还原为源码。我们终于有了面向整个应用、可以阅读、修改和继续开发的源码基础。

**MikuDanceStudio NG，就从这里开始。**

我们希望一起做一个更好的 MMD：留住它在操作上的用心，让现代图形技术有施展的空间，也让更多人能在自己日常使用的电脑上创作。那些陪伴我们多年的模型、动作、特效和场景，也应该拥有继续生长的机会。

八年前那一晚没有聊完的问题，今天，我们终于可以接着做下去。

## 我们正在做什么

- **熟悉的创作方式。** 骨骼、表情、IK、物理、相机、照明、附件、时间轴与撤销重做，以原版 MMD 的行为为对齐依据。
- **让已有作品延续。** 支持 PMD／PMX 模型、VMD 动作、VPD 姿态和 PMM 工程，用真实场景和可公开的生成夹具验证兼容性修复。
- **现代渲染基础。** 通过共享 RHI 接入 DirectX 11、DirectX 12、Vulkan 和原生 Metal，逐步打通材质、自阴影、多渲染目标、HDR 中间目标与后处理。
- **更多人能读懂的界面。** 三平台共用 Qt 编辑器，提供 15 种界面语言，在 **文件 → 首选项 → 语言** 中选择。
- **为 MME 和 Ray-MMD 铺路。** 建立共享的效果描述、资源和 Pass 调度，按效果逐项扩展、验证兼容范围。

## 项目现在走到哪里

NG 仍在积极开发中。可移植内核、CPU 场景运行时和跨平台渲染基础对应的 **P0–P3 已完成**。**P4 编辑器已按调整后的范围阶段验收**，真实混合 DPI 跨屏测试作为已知限制延后补验；下一阶段为 [P5 跨平台 MME](docs/CROSS_PLATFORM_P5.md)。完整 MME 与 Ray-MMD 兼容仍在推进，当前支持范围以已经记录和验证的效果子集为准。

| 平台 | NG 渲染后端 | 当前开发基线 |
| --- | --- | --- |
| Windows | DirectX 11、DirectX 12、Vulkan | Windows 10／11，x64 |
| macOS | 原生 Metal；可选 Vulkan／MoltenVK | macOS 13+，Apple Silicon |
| Linux | Vulkan，支持 X11 与 Wayland | Ubuntu 22.04，x86_64 |

已验证的能力和剩余工作，见[当前验收状态](docs/P4_ACCEPTANCE_STATUS.md)、[跨平台计划](cross_platform_plan.md)与 [RHI 功能表](docs/RHI_FEATURE_SUPPORT.md)。开发包可在 [Portable editor 工作流](https://github.com/jstzwj/MikuDanceStudio-NG/actions/workflows/portable-editor.yml)中获取。

本仓库由 [MikuDanceStudio](https://github.com/jstzwj/MikuDanceStudio) 的原 `next` 分支迁入，保留完整提交历史。原仓库继续维护 Windows 兼容分支，NG 在自己的 `main` 分支上独立向前。

## 从源码构建

基础依赖为 C++17 编译器、CMake 3.21 或更新版本、Python 3，以及带 Widgets、Multimedia 和 Linguist 工具的 Qt 6.8.3。渲染后端还需要对应平台 SDK 和着色器工具；DXC、Vulkan、Metal 及 Linux 系统依赖的配置见[构建与打包指南](docs/DISTRIBUTION.md)。

```sh
git clone https://github.com/jstzwj/MikuDanceStudio-NG.git
cd MikuDanceStudio-NG
cmake -S cmake/portable-editor -B out/build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build out/build --config Release --target dist --parallel 3
```

`out/dist` 包含应用和运行依赖。Windows 运行 `out/dist/MikuDanceStudioNG.exe`；macOS 打开其中的 `.app`；Linux 运行 `out/dist/bin/MikuDanceStudioNG`。移动到其他电脑时，请保留整个发布目录。构建文件位于 `out/build`，打包归档位于 `out/releases`。

## 一起写下一章

一个表现不同的场景、一个加载失败的特效、一句更自然的翻译，或是一份补丁，都能让项目向前走一点。尽量提供小而可复现的例子，帮助我们把日常使用中真正重要的细节留下来。反馈问题时，请附上平台、渲染后端和复现步骤。

[反馈问题](https://github.com/jstzwj/MikuDanceStudio-NG/issues) · [文档索引](docs/README.md) · [翻译指南](translations/README.md) · [发布指南](docs/RELEASES.md) · [版本](VERSION)

## 感谢

感谢 **樋口優（Yu Higuchi）** 创造了 [MikuMikuDance](https://learnmmd.com/downloads/)，让那么多人有了开始创作的地方。感谢 **Rui** 带来的 Ray-MMD，感谢他为寻找下一条路投入的岁月，也感谢八年前那一晚的长谈。

感谢模型、动作、特效、音乐与教程的作者，感谢 MME 社区，感谢每一位愿意分享经验的人。正是这些积累，让 MMD 成为值得继续做下去的事情。也感谢 Qt、Bullet Physics，以及本项目所使用的开源工具的维护者。

## 许可与来源

MikuDanceStudio 自有代码采用 [MIT 许可](LICENSE)。原版 MMD 资源保留原有条款，相关[许可声明](licenses/MikuMikuDance.txt)与[资源来源](res/README.md)单独记录。第三方组件遵循各自的许可证。
