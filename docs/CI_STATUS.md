# GitHub Actions 失败核查

核查日期：2026-11-05。核查基线：`af2576c`。

## 近期反复失败的原因

| 工作流／记录 | 实际结果 | 原因与处理 |
|---|---|---|
| Portable rendering：[41993955253](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41993955253) | Linux、macOS Vulkan 和原生 Metal 均只在通用着色器预算测试失败；编译及其余渲染测试通过 | `2d98e3e` 将固定帧常量空间改为按需增长，上限为 32 MiB；测试仍把 40 次 64 KiB 绘制当作溢出。合法帧不再被拒绝，旧断言失败。现改为根据后端公布的上限、对齐和常量占用检查边界及边界外一帧 |
| Portable rendering：[41992189986](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41992189986) | 同样三个渲染任务失败，DXC 工具任务通过 | 工作流没有 Markdown 路径过滤，后续 README 提交继续重复触发未修复的门禁，并非 README 改坏渲染器 |
| Portable editor：[41972392409](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41972392409) | Windows 真实客户区外拖选专项失败 | 当时 18 个实际拖选案例存在差异；Unicode 编辑及传统 CJK 绘制检查通过。该历史失败保留，后续 [41975627370](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41975627370) 已补验通过 |
| Portable editor：[41976890117](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41976890117) | Linux／macOS 定向显示态补验通过 | 修复独立截图复看发现的父窗口裁切；详见 [P4 显示态资格](P4_SHOWN_UI_QUALIFICATION.md) |
| Portable editor：[41982511296](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41982511296)、[41992189990](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41992189990) | Qt 翻译迁移后的 Windows、Linux、macOS 编辑器任务全部通过 | 最新编辑器工作流与持续失败的渲染工作流应分开判断，不能把 Actions 列表的历史红灯当作当前所有任务都失败 |

较早的原生 IME、焦点、多行输入、字形及整帧失败的修复与适用范围保留在
[P4 当前验收表](P4_ACCEPTANCE_STATUS.md)及其链接的历史证据中。此次只核对上述
运行的具体任务和失败步骤，没有把所有历史运行重新执行一遍。

## 本轮修复与验证

- 通用着色器测试根据 `maxFrameConstantBytes` 计算可容纳的最大绘制数量，验证
  界内准备成功，下一次绘制按预算原因拒绝且不发生上传；拒绝后原有准备帧仍可提交。
- 将既有 1,024 次绘制的动态扩容、交替大小场景、GPU 帧槽轮转和精确输出测试
  纳入 Vulkan 与原生 Metal 的定期渲染任务。
- Portable rendering／formats 的 push 和 pull request 排除纯 Markdown／文档变更；
  editor 的 pull request 与已有 push 规则保持一致。代码和文档混合修改仍触发检查。
- rendering／formats 对同一 ref 的自动任务取消被新提交取代的旧运行；手动运行
  使用独立并发组，不被后续自动提交取消。保留手动完整检查入口。

本地已复现旧通用着色器测试的 Vulkan 失败。修复后五项定向 CTest 全部通过：
着色器契约、帧预算、DX11／DX12／Vulkan 通用着色器；另直接通过 Vulkan 和
DX12 WARP 的 1,024 次绘制测试，以及 DX12 WARP 通用着色器测试。
`actionlint`、clang-format 和差异空白检查通过。

远端 [41996325208](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41996325208)
在 `4d1f343` 上全部通过：Linux 软件 Vulkan 12/12、macOS MoltenVK 15/15、
原生 Metal 10/10，均无跳过。Linux X11／Wayland 呈现、场景及性能专项也通过。
Metal 任务启用了 API 验证；其设备为 Apple Paravirtual device，不扩大为实体 GPU 验收。
原本持续失败的三个任务已在同一轮修复验证中恢复。
本轮没有修改运行时绘制算法、放宽图像阈值或跳过失败测试。

同一提交的其余三个工作流也全部通过：

- [Portable editor 41996325186](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41996325186)：
  Windows 93/93、Linux 81/81、macOS 76/76 项常规 P4 测试，均无跳过；
  三平台呈现、音频时钟、发布包重定位及 15 种语言检查通过。
- [Portable formats 41996325090](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41996325090)：
  Linux、macOS、Windows x64／Win32 四组基础运行时构建及重复测试通过。
- [Bullet build regressions 41996325360](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41996325360)：
  x86／x64、Debug／Release 与两版 CMake 的八组构建回归通过。

这是一次代码推送触发的验证，没有额外手动 dispatch 或重跑。编辑器的这些常规门禁
不替代先前记录的实际原生 IME、显示态专项，也不包含尚未执行的真实跨屏测试。

本地报告、远端运行和下载报告的哈希见[版本化记录](evidence/ci-budget-2026-11-05.json)。

## P4 仍需什么

本次重新查询仓库 runner 清单，仍为 `total_count=0`。用户也确认暂时没有独立双屏环境。
真实混合 DPI 跨屏测试程序已编译，但不能在当前环境取得运行证据。继续保留
**P4／C1 未验收**；不将 CI 绿灯或模拟缩放当作真实跨屏实测。
执行条件和命令见 [真实混合 DPI 验收](P4_PHYSICAL_DPI.md)。

以上是本次 CI 结束时的结论。随后用户同意将真实跨屏项目延后，按调整范围放行
P4 并进入 P5；当前阶段决定见 [P4 验收表](P4_ACCEPTANCE_STATUS.md)。本轮版本化
CI 证据保留运行当时的状态，不把阶段决定改写成已经执行了跨屏测试。
