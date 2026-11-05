# 真实混合 DPI 验收

**状态：测试程序已编译，实际跨屏尚未执行，不能据此关闭 P4。**

2026-11-05 再次核对仓库的 Actions runner 清单：`total_count=0`，没有可使用的
独立自托管会话。本地仍不使用工作桌面做跨屏或输入测试。已编译程序的本地保护分支
继续在创建 QApplication 前拒绝执行；这只是保护验证，不是实际 DPI 验收结果。

`qt_monitor_transition_test` 是 Windows 专用的显式执行目标，不注册进普通 CTest。
必须在独立、自托管的交互 runner 上执行，该会话不能是用户正在工作的桌面。
它不修改显示器配置、系统缩放或输入法。需要已配置两个实际 DPI 不同的 OS 显示器；
单个 RDP 屏、环境变量模拟缩放和两个相同 DPI 显示器均不能取得资格。

程序在创建 QApplication 前检查 `GITHUB_ACTIONS=true` 与
`MDS_QT_ISOLATED_DISPLAY=1`，拒绝 `QT_SCALE_FACTOR`、`QT_SCREEN_SCALE_FACTORS`、
`QT_FONT_DPI`、`QT_DEVICE_PIXEL_RATIO`。本地只能验证拒绝分支，禁止伪造环境运行。

独立 runner 上的命令为：

```powershell
cmake --build out/build --config Release --target qt_monitor_transition_test
$env:MDS_QT_ISOLATED_DISPLAY='1'
out/build/Release/qt_monitor_transition_test.exe res/assets out/evidence/physical-dpi/report.json
```

Linux 风格单配置 Ninja 构建的可执行文件没有 `Release/` 子目录。测试本身仅支持
Windows QPA；后续 macOS／Linux 真实混合 DPI 必须另行记录，不扩大本项的范围。

检查覆盖三种 UI 语言，每种按 A → B → A 移动**真实编辑器窗口**，要求：

- `QWindow::screen()`、目标 `QScreen`、顶层与嵌入视口 HWND 的
  `GetDpiForWindow`、实际 DPR 一致。
- 实际至少收到六次 `WM_DPICHANGED`，不能用手工发送消息代替。
- DX11 WARP 的共享最终颜色目标尺寸等于视口实际物理像素；恢复稳定后复用准备结果。
- 当前帧输入框的 IME 光标矩形有效；保存九张实际编辑器窗口图像用于独立外观复验。
- 语言／显示器切换不修改文档、不推进动画或物理。

程序不做后台自动运行、不更改用户显示设置；缺少前提时明确失败。该专项也不替代
Metal／Vulkan 跨屏、实际 IME 候选窗口位置、鼠标命中和截图的独立视觉验收。
