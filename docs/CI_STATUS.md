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

远端 Linux／macOS／Metal 的本轮结果将在单次代码提交的自动任务结束后记录。
本轮没有修改运行时绘制算法、放宽图像阈值或跳过失败测试。

## P4 仍需什么

本次重新查询仓库 runner 清单，仍为 `total_count=0`。用户也确认暂时没有独立双屏环境。
真实混合 DPI 跨屏测试程序已编译，但不能在当前环境取得运行证据。继续保留
**P4／C1 未验收**；不将 CI 绿灯或模拟缩放当作真实跨屏实测。
执行条件和命令见 [真实混合 DPI 验收](P4_PHYSICAL_DPI.md)。
